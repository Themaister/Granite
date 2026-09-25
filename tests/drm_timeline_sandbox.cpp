#include "context.hpp"
#include "device.hpp"
#include "semaphore.hpp"
#include "logging.hpp"
#include <xf86drm.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <thread>
#include <mutex>
#include <condition_variable>

extern "C" {
typedef struct kmt_fence_device_opaque *kmt_fence_device;
typedef struct kmt_fence_handle_opaque *kmt_fence_handle;

kmt_fence_device kmt_fence_device_create(int drm_fd);
kmt_fence_device kmt_fence_device_create_from_drm_properties(
	const VkPhysicalDeviceDrmPropertiesEXT *drm_properties);
int kmt_fence_device_get_drmfd(kmt_fence_device device);
void kmt_fence_device_destroy(kmt_fence_device device);

kmt_fence_handle kmt_fence_device_create_fence(kmt_fence_device device, uint64_t initial_value);
void kmt_fence_device_destroy_fence(kmt_fence_device device, kmt_fence_handle fence);

bool kmt_fence_device_register_signal_immediate(kmt_fence_device device, kmt_fence_handle fence, uint64_t value);
bool kmt_fence_device_register_signal(kmt_fence_device device, kmt_fence_handle fence,
                                      uint32_t drm_timeline, uint64_t point, uint64_t value);

uint64_t kmt_fence_device_register_edge(kmt_fence_device device, kmt_fence_handle fence, uint64_t value);
bool kmt_fence_device_edge_signal_eventfd(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge, int eventfd);
bool kmt_fence_device_edge_wait_materialization(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge, int *sync_fd);
void kmt_fence_device_unregister_edge(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge);
}

struct kmt_epoll_data
{
	kmt_fence_handle fence;
	uint64_t order;
	int fd;
};

struct kmt_fence_device_opaque
{
	int drmfd = -1;
	int epoll_fd = -1;
	int wake_fd = -1;
	int pipe_fd = -1;
	std::atomic_uint64_t order_count{};
	std::thread epoll_thread;

	uint64_t allocate_order()
	{
		return order_count.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	void epoll_main();
};

struct kmt_pending_signal
{
	uint64_t order;
	uint64_t value;
	uint32_t sync_handle;
};

struct kmt_pending_edge
{
	uint64_t order;
	uint64_t value;
	int eventfd; // or HANDLE.
	uint32_t sync_handle;
	bool materialized;
};

struct kmt_fence_handle_opaque
{
	kmt_fence_device device = nullptr;
	uint64_t current_value = 0;

	std::mutex lock;
	std::condition_variable cond;

	std::vector<kmt_pending_signal> pending_signals;
	std::vector<kmt_pending_edge> pending_edges;

	void complete(uint64_t order);
	void signal_immediate_locked(uint64_t value);
};

void kmt_fence_device_opaque::epoll_main()
{
	std::vector<epoll_event> events;
	events.resize(256);
	bool alive = true;

	std::vector<kmt_epoll_data> wake_data;

	while (alive)
	{
		int ret = epoll_wait(epoll_fd, events.data(), events.size(), -1);

		if (ret == int(events.size()))
		{
			// For signal ordering reasons, we need to receive every signaled fd at once.
			// This should basically never happen in practice.
			// Make the buffer larger and repoll.
			events.resize(events.size() * 2);
			continue;
		}

		if (ret <= 0)
			break;

		wake_data.clear();

		for (int i = 0; i < ret; i++)
		{
			if (events[i].data.ptr == nullptr)
			{
				kmt_epoll_data data = {};
				while (read(pipe_fd, &data, sizeof(data)) == sizeof(data))
				{
					if (data.fence == nullptr)
						alive = false;
					else
						wake_data.push_back(data);
				}
			}
			else
			{
				wake_data.push_back(*static_cast<const kmt_epoll_data *>(events[i].data.ptr));
			}
		}

		// Ensure signal order. If a signal was registered before another, and both events are signaled,
		// we must preserve the global ordering.
		std::sort(wake_data.begin(), wake_data.end(), [](const kmt_epoll_data &a, const kmt_epoll_data &b)
		{
			return a.order < b.order;
		});

		for (auto &wake : wake_data)
		{
			wake.fence->complete(wake.order);
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wake.fd, nullptr);
		}
	}
}

kmt_fence_device kmt_fence_device_create(int drm_fd)
{
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) < 0)
		return nullptr;

	auto *dev = new kmt_fence_device_opaque();
	dev->pipe_fd = fds[0];
	dev->wake_fd = fds[1];

	if (fcntl(dev->pipe_fd, F_SETFL, fcntl(dev->pipe_fd, F_GETFL) | O_NONBLOCK) < 0)
	{
		delete dev;
		return nullptr;
	}

	dev->drmfd = drm_fd;
	dev->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (dev->epoll_fd < 0)
	{
		delete dev;
		return nullptr;
	}

	epoll_event wake_dummy = {};
	wake_dummy.events = EPOLLIN;
	if (epoll_ctl(dev->epoll_fd, EPOLL_CTL_ADD, dev->pipe_fd, &wake_dummy) < 0)
	{
		delete dev;
		return nullptr;
	}

	try
	{
		dev->epoll_thread = std::thread(&kmt_fence_device_opaque::epoll_main, dev);
	}
	catch (const std::exception &)
	{
		delete dev;
		return nullptr;
	}

	return dev;
}

kmt_fence_device kmt_fence_device_create_from_drm_properties(const VkPhysicalDeviceDrmPropertiesEXT *drm_properties)
{
	if (!drm_properties->hasRender)
		return nullptr;

	char path[128];
	snprintf(path, sizeof(path), "/dev/dri/renderD%u", uint32_t(drm_properties->renderMinor));
	int fd = open(path, O_RDWR);
	if (fd < 0)
		return nullptr;

	auto *dev = kmt_fence_device_create(fd);
	if (!dev)
		close(fd);
	return dev;
}

int kmt_fence_device_get_drmfd(kmt_fence_device device)
{
	return device->drmfd;
}

void kmt_fence_device_destroy(kmt_fence_device device)
{
	if (device->epoll_thread.joinable())
	{
		kmt_epoll_data dummy = {};
		write(device->epoll_fd, &dummy, sizeof(dummy));
		device->epoll_thread.join();
	}

	if (device->drmfd >= 0)
		close(device->drmfd);
	if (device->epoll_fd >= 0)
		close(device->epoll_fd);
	if (device->wake_fd >= 0)
		close(device->wake_fd);
	if (device->pipe_fd >= 0)
		close(device->pipe_fd);
	delete device;
}

kmt_fence_handle kmt_fence_device_create_fence(kmt_fence_device device, uint64_t initial_value)
{
	auto *fence = new kmt_fence_handle_opaque();
	fence->device = device;
	fence->current_value = initial_value;
	return fence;
}

void kmt_fence_device_destroy_fence(kmt_fence_device device, kmt_fence_handle fence)
{
	for (auto &signal : fence->pending_signals)
		drmSyncobjDestroy(device->drmfd, signal.sync_handle);
	delete fence;
}

void kmt_fence_handle_opaque::signal_immediate_locked(uint64_t value)
{
	current_value = value;

	size_t i = 0;
	while (i < pending_edges.size())
	{
		auto &edge = pending_edges[i];
		if (value >= edge.value)
		{
			if (edge.eventfd >= 0)
			{
				uint64_t sig = 1;
				write(edge.eventfd, &sig, sizeof(sig));
			}

			pending_edges[i] = pending_edges.back();
			pending_edges.pop_back();
		}
		else
		{
			i++;
		}
	}

	cond.notify_all();
}

bool kmt_fence_device_register_signal_immediate(kmt_fence_device, kmt_fence_handle fence, uint64_t value)
{
	std::lock_guard<std::mutex> holder{fence->lock};
	fence->signal_immediate_locked(value);
	return true;
}

void kmt_fence_handle_opaque::complete(uint64_t order)
{
	std::lock_guard<std::mutex> holder{lock};

	auto itr = std::find_if(pending_signals.begin(), pending_signals.end(),
	                        [&](const kmt_pending_signal &sig)
	                        {
		                        return sig.order == order;
	                        });

	if (itr == pending_signals.end())
		std::terminate();

	uint64_t signal_value = itr->value;

	drmSyncobjDestroy(device->drmfd, itr->sync_handle);

	*itr = pending_signals.back();
	pending_signals.pop_back();

	signal_immediate_locked(signal_value);
}

bool kmt_fence_device_register_signal(kmt_fence_device device, kmt_fence_handle fence,
                                      uint32_t drm_timeline, uint64_t point, uint64_t value)
{
	uint32_t sync_handle;
	if (drmSyncobjCreate(device->drmfd, 0, &sync_handle) < 0)
		return false;

	if (drmSyncobjTransfer(device->drmfd, sync_handle, 0, drm_timeline, point, 0) < 0)
	{
		drmSyncobjDestroy(device->drmfd, sync_handle);
		return false;
	}

	uint64_t order = device->allocate_order();

	{
		std::lock_guard<std::mutex> holder{fence->lock};
		kmt_pending_signal sig = {};
		sig.value = value;
		sig.order = order;
		sig.sync_handle = sync_handle;
		fence->pending_signals.push_back(sig);
		fence->cond.notify_all();
	}

	int sync_fd;
	if (drmSyncobjHandleToFD(device->epoll_fd, sync_handle, &sync_fd) < 0)
	{
		drmSyncobjDestroy(device->drmfd, sync_handle);
		return false;
	}

	// Materialize the wait if we can unblock a thread.
	for (auto &edge : fence->pending_edges)
	{
		// Materialize with the first submitted signal that could unblock the waiter.
		if (value >= edge.value && !edge.materialized)
		{
			edge.materialized = true;
			edge.sync_handle = sync_handle;
		}
	}

	if (sync_fd >= 0)
	{
		epoll_event ev = {};
		ev.events = EPOLLIN;

		auto *data = new kmt_epoll_data();
		data->order = order;
		data->fence = fence;
		ev.data.ptr = data;

		if (epoll_ctl(device->epoll_fd, EPOLL_CTL_ADD, sync_fd, &ev) < 0)
		{
			drmSyncobjDestroy(device->drmfd, sync_handle);
			return false;
		}
	}
	else
	{
		// Could happen if the sync is already complete?
		// Need to ensure signal order though.

		kmt_epoll_data data = { fence, order };
		if (write(device->wake_fd, &data, sizeof(data)) < 0)
		{
			drmSyncobjDestroy(device->drmfd, sync_handle);
			return false;
		}
	}

	return true;
}

uint64_t kmt_fence_device_register_edge(kmt_fence_device device, kmt_fence_handle fence, uint64_t value)
{
	uint64_t order = device->allocate_order();
	std::lock_guard<std::mutex> holder{fence->lock};

	// The wait can be satisfied instantly.
	if (fence->current_value >= value)
		return 0;

	kmt_pending_edge edge = {};
	edge.order = order;
	edge.value = value;
	fence->pending_edges.push_back(edge);
	return order;
}

static kmt_pending_edge *kmt_fence_find_pending_edge_locked(kmt_fence_device, kmt_fence_handle fence, uint64_t edge)
{
	auto itr = std::find_if(fence->pending_edges.begin(), fence->pending_edges.end(),
	                        [&](const kmt_pending_edge &pending)
	                        {
		                        return pending.order == edge;
	                        });

	return itr == fence->pending_edges.end() ? nullptr : &(*itr);
}

bool kmt_fence_device_edge_signal_eventfd(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge, int eventfd)
{
	std::lock_guard<std::mutex> holder{fence->lock};
	auto *pending = kmt_fence_find_pending_edge_locked(device, fence, edge);

	if (!pending)
	{
		const uint64_t dummy = 1;
		return write(eventfd, &dummy, sizeof(dummy)) > 0;
	}
	else
	{
		pending->eventfd = eventfd;
		return true;
	}
}

bool kmt_fence_device_edge_wait_materialization(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge, int *sync_fd)
{
	std::unique_lock<std::mutex> holder{fence->lock};
	fence->cond.wait(holder, [&]()
	{
		auto *pending = kmt_fence_find_pending_edge_locked(device, fence, edge);
		if (!pending)
			return true;
		return pending->materialized;
	});

	auto *pending = kmt_fence_find_pending_edge_locked(device, fence, edge);
	if (pending)
	{
		*sync_fd = -1;
		return true;
	}
	else
	{
		return drmSyncobjHandleToFD(device->drmfd, pending->sync_handle, sync_fd) == 0;
	}
}

void kmt_fence_device_unregister_edge(kmt_fence_device, kmt_fence_handle fence, uint64_t edge)
{
	std::lock_guard<std::mutex> holder{fence->lock};

	auto itr = std::find_if(fence->pending_edges.begin(), fence->pending_edges.end(),
							[&](const kmt_pending_edge &pending)
							{
								return pending.order == edge;
							});

	if (itr != fence->pending_edges.end())
	{
		*itr = fence->pending_edges.back();
		fence->pending_edges.pop_back();
	}
}

using namespace Granite;
using namespace Vulkan;

static int open_drm_fd(uint32_t render_minor)
{
	char path[128];
	snprintf(path, sizeof(path), "/dev/dri/renderD%u", render_minor);
	return open(path, O_RDWR);
}

static bool import_timeline(int drm_fd, int fd, uint32_t *handle)
{
	if (drmSyncobjFDToHandle(drm_fd, fd, handle) < 0)
	{
		LOGE("Failed to import timeline.\n");
		return false;
	}

	::close(fd);
	return true;
}

static void run_test(Device &device)
{
	if (!device.get_device_features().supports_drm_properties)
		return;

	const auto &props = device.get_device_features().drm_properties;

	if (!props.hasRender)
		return;

	int drm_fd = open_drm_fd(props.renderMinor);
	if (drm_fd < 0)
		return;

	// On Mesa, this is always DRM timeline.
	// There is a public EXT in flight that exposes DRM timeline properly for everyone.
	auto timeline = device.request_semaphore_external(
		VK_SEMAPHORE_TYPE_TIMELINE, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);

	auto exported = timeline->export_to_handle();
	if (!exported)
	{
		LOGE("Failed to export.\n");
		return;
	}

	uint32_t drm_timeline;
	if (!import_timeline(drm_fd, exported.handle, &drm_timeline))
	{
		LOGE("Failed to import timeline.\n");
		return;
	}

	uint32_t sync_handle;
	if (drmSyncobjCreate(drm_fd, 0, &sync_handle) < 0)
	{
		LOGE("Failed to create handle.\n");
		return;
	}

	uint32_t first_signaled;
	uint64_t point = 8;
	int ret = drmSyncobjTimelineWait(drm_fd, &drm_timeline, &point, 1, 0,
		DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
		DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE, &first_signaled);

	if (ret == -ETIME)
	{
		LOGI("Wait is not available.\n");
	}
	else if (ret < 0)
	{
		LOGE("Failed to wait.\n");
		return;
	}

	{
		auto binary = device.request_timeline_semaphore_as_binary(*timeline, 10);
		device.submit_empty(CommandBuffer::Type::Generic, nullptr, binary.get());
	}

	ret = drmSyncobjTimelineWait(drm_fd, &drm_timeline, &point, 1, 1000000000,
		DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL | DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE, &first_signaled);

	if (ret < 0)
	{
		LOGE("Failed to wait.\n");
		return;
	}

	if (drmSyncobjTransfer(drm_fd, sync_handle, 0, drm_timeline, 8, 0) < 0)
	{
		LOGE("Failed to transfer.\n");
		return;
	}

	int sync_fd;
	if (drmSyncobjExportSyncFile(drm_fd, sync_handle, &sync_fd) < 0)
	{
		LOGE("Failed to export sync file.\n");
		return;
	}

	if (sync_fd >= 0)
	{
		pollfd fd = {};
		fd.fd = sync_fd;
		fd.events = POLLIN;
		if (poll(&fd, 1, -1) != 1 || (fd.revents & POLLIN) == 0)
			LOGE("Failed to poll?\n");
	}

	close(sync_fd);

	if (drmSyncobjDestroy(drm_fd, drm_timeline) < 0)
	{
		LOGE("Failed to destroy?\n");
		return;
	}
}

int main()
{
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context ctx;
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device dev;
	dev.set_context(ctx);

	run_test(dev);
}