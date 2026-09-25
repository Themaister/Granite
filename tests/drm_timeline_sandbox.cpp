#include "context.hpp"
#include "device.hpp"
#include "semaphore.hpp"
#include "logging.hpp"
#include <xf86drm.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <csignal>
#include <future>
#include "thread_id.hpp"
#include "thread_name.hpp"

extern "C" {
typedef struct kmt_fence_device_opaque *kmt_fence_device;
typedef struct kmt_fence_handle_opaque *kmt_fence_handle;

kmt_fence_device kmt_fence_device_create(int drm_fd);
kmt_fence_device kmt_fence_device_create_from_drm_properties(
	const VkPhysicalDeviceDrmPropertiesEXT *drm_properties);
int kmt_fence_device_get_drmfd(kmt_fence_device device);
void kmt_fence_device_destroy(kmt_fence_device device);
bool kmt_fence_device_import_timeline(kmt_fence_device device, int fd, uint32_t *syncobj);

kmt_fence_handle kmt_fence_device_create_fence(kmt_fence_device device, uint64_t initial_value);
void kmt_fence_device_destroy_fence(kmt_fence_device device, kmt_fence_handle fence);

bool kmt_fence_device_register_signal_immediate(kmt_fence_device device, kmt_fence_handle fence, uint64_t value);
bool kmt_fence_device_register_signal(kmt_fence_device device, kmt_fence_handle fence,
                                      uint32_t drm_timeline, uint64_t point, uint64_t value);
bool kmt_fence_device_register_sync_file(kmt_fence_device device, kmt_fence_handle fence, int fd, uint64_t value);

uint64_t kmt_fence_device_query_fence(kmt_fence_device device, kmt_fence_handle fence);
uint64_t kmt_fence_device_register_edge(kmt_fence_device device, kmt_fence_handle fence, uint64_t value, int eventfd);
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
	int eventfd; // or ntsync, or HANDLE.
	int syncfd; // A binary semaphore.
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
	Util::set_current_thread_name("epoll");
	std::vector<epoll_event> events;
	events.resize(256);
	bool alive = true;

	std::vector<kmt_epoll_data> wake_data;

	while (alive)
	{
		int ret = epoll_wait(epoll_fd, events.data(), events.size(), -1);

		if (ret == -1 && errno == EINTR)
			continue;

		if (ret < 0)
			LOGE("Failed to epoll_wait(), errno = %d\n", errno);

		if (ret <= 0)
			break;

		if (ret == int(events.size()))
		{
			// For signal ordering reasons, we need to receive every signaled fd at once.
			// This should basically never happen in practice.
			// Make the buffer larger and repoll.
			events.resize(events.size() * 2);
			continue;
		}

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
				auto *ptr = static_cast<kmt_epoll_data *>(events[i].data.ptr);
				wake_data.push_back(*ptr);
				delete ptr;
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
			if (wake.fd >= 0)
			{
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wake.fd, nullptr);
				close(wake.fd);
			}
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

	// Writer must be blocking so we avoid losing wakeups spuriously under pressure.
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

bool kmt_fence_device_import_timeline(kmt_fence_device device, int fd, uint32_t *syncobj)
{
	if (drmSyncobjFDToHandle(device->drmfd, fd, syncobj) < 0)
	{
		LOGE("Failed to import timeline.\n");
		return false;
	}

	close(fd);
	return true;
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

	for (auto &edge : fence->pending_edges)
		if (edge.syncfd >= 0)
			close(edge.syncfd);

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

			if (edge.syncfd >= 0)
				close(edge.syncfd);

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
	uint32_t first_signaled;
	int ret = drmSyncobjTimelineWait(device->drmfd, &drm_timeline, &point, 1, 0,
		DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL | DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE, &first_signaled);
	if (ret < 0)
		return false;

	uint32_t sync_handle;
	if (drmSyncobjCreate(device->drmfd, 0, &sync_handle) < 0)
		return false;

	if (drmSyncobjTransfer(device->drmfd, sync_handle, 0, drm_timeline, point, 0) < 0)
	{
		drmSyncobjDestroy(device->drmfd, sync_handle);
		return false;
	}

	uint64_t order = device->allocate_order();

	int sync_fd;
	ret = drmSyncobjExportSyncFile(device->drmfd, sync_handle, &sync_fd);
	if (ret < 0)
	{
		drmSyncobjDestroy(device->drmfd, sync_handle);
		return false;
	}

	{
		std::lock_guard<std::mutex> holder{fence->lock};

		kmt_pending_signal sig = {};
		sig.value = value;
		sig.order = order;
		sig.sync_handle = sync_handle;
		fence->pending_signals.push_back(sig);

		bool has_materialization = false;

		// Materialize the wait if we can unblock a thread.
		for (auto &edge : fence->pending_edges)
		{
			// Materialize with the first submitted signal that could unblock the waiter.
			if (value >= edge.value && edge.eventfd < 0 && edge.syncfd < 0)
			{
				edge.syncfd = dup(sync_fd);
				has_materialization = true;
			}
		}

		if (has_materialization)
			fence->cond.notify_all();
	}

	if (sync_fd >= 0)
	{
		epoll_event ev = {};
		ev.events = EPOLLIN;

		auto *data = new kmt_epoll_data();
		data->order = order;
		data->fence = fence;
		data->fd = sync_fd;
		ev.data.ptr = data;

		ret = epoll_ctl(device->epoll_fd, EPOLL_CTL_ADD, sync_fd, &ev);

		if (ret < 0)
		{
			LOGE("Failed to add syncfd to epoll, ret %d, errno %d\n", ret, errno);
			drmSyncobjDestroy(device->drmfd, sync_handle);
			delete data;
			return false;
		}
	}
	else
	{
		// Could this happen if the sync is already complete?
		// Vulkan spec talks about this case at least ...
		// Need to ensure signal order though.

		kmt_epoll_data data = { fence, order, -1 };
		if (write(device->wake_fd, &data, sizeof(data)) < 0)
		{
			drmSyncobjDestroy(device->drmfd, sync_handle);
			return false;
		}
	}

	return true;
}

bool kmt_fence_device_register_sync_file(kmt_fence_device device, kmt_fence_handle fence, int fd, uint64_t value)
{
	uint32_t handle;

	if (drmSyncobjCreate(device->drmfd, 0, &handle) < 0)
		return false;
	if (drmSyncobjImportSyncFile(device->drmfd, handle, fd) < 0)
		return false;

	if (!kmt_fence_device_register_signal(device, fence, handle, 0, value))
	{
		drmSyncobjDestroy(device->drmfd, handle);
		return false;
	}

	drmSyncobjDestroy(device->drmfd, handle);
	close(fd);
	return true;
}

uint64_t kmt_fence_device_query_fence(kmt_fence_device, kmt_fence_handle fence)
{
	std::lock_guard<std::mutex> holder{fence->lock};
	return fence->current_value;
}

uint64_t kmt_fence_device_register_edge(kmt_fence_device device, kmt_fence_handle fence, uint64_t value, int eventfd)
{
	uint64_t order = device->allocate_order();
	std::lock_guard<std::mutex> holder{fence->lock};

	// The wait can be satisfied instantly.
	if (fence->current_value >= value)
	{
		if (eventfd >= 0)
		{
			uint64_t dummy = 1;
			write(eventfd, &dummy, sizeof(dummy));
		}

		return 0;
	}

	kmt_pending_edge edge = {};
	edge.order = order;
	edge.value = value;
	edge.syncfd = -1;
	edge.eventfd = eventfd;
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

static void kmt_fence_device_unregister_edge_locked(kmt_fence_device, kmt_fence_handle fence, uint64_t edge)
{
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

bool kmt_fence_device_edge_wait_materialization(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge, int *sync_fd)
{
	std::unique_lock<std::mutex> holder{fence->lock};
	fence->cond.wait(holder, [&]()
	{
		auto *pending = kmt_fence_find_pending_edge_locked(device, fence, edge);

		// Already complete, nothing to wait for.
		if (!pending)
			return true;

		// We have a binary semaphore to wait for.
		return pending->syncfd >= 0;
	});

	auto *pending = kmt_fence_find_pending_edge_locked(device, fence, edge);

	if (pending)
	{
		*sync_fd = -1;
	}
	else
	{
		*sync_fd = pending->syncfd;
		pending->syncfd = -1;
	}

	kmt_fence_device_unregister_edge_locked(device, fence, edge);

	return true;
}

void kmt_fence_device_unregister_edge(kmt_fence_device device, kmt_fence_handle fence, uint64_t edge)
{
	std::lock_guard<std::mutex> holder{fence->lock};
	kmt_fence_device_unregister_edge_locked(device, fence, edge);
}

using namespace Granite;
using namespace Vulkan;

struct DRMTimeline
{
	Semaphore sem;
	int drmfd = -1;
	uint32_t drm_timeline = 0;

	~DRMTimeline()
	{
		if (drmfd >= 0)
			drmSyncobjDestroy(drmfd, drm_timeline);
	}
};

static DRMTimeline create_drm_timeline_from_granite(Device &device, kmt_fence_device kmt_dev)
{
	DRMTimeline tl = {};
	// On Mesa, this is always DRM timeline.
	// There is a public EXT in flight that exposes DRM timeline properly for everyone.
	tl.sem = device.request_semaphore_external(VK_SEMAPHORE_TYPE_TIMELINE, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);

	auto exported = tl.sem->export_to_handle();
	if (!exported)
		LOGE("Failed to export.\n");

	if (!kmt_fence_device_import_timeline(kmt_dev, exported.handle, &tl.drm_timeline))
		LOGE("Failed to import timeline.\n");

	tl.drmfd = kmt_fence_device_get_drmfd(kmt_dev);
	return tl;
}

static void run_test(Device &device)
{
	if (!device.get_device_features().supports_drm_properties)
		return;

	const auto &props = device.get_device_features().drm_properties;
	kmt_fence_device kmt_dev = kmt_fence_device_create_from_drm_properties(&props);

	if (!kmt_dev)
		return;

	kmt_fence_handle kmt_fence = kmt_fence_device_create_fence(kmt_dev, 0);

	BufferCreateInfo bufinfo = {};
	bufinfo.domain = BufferDomain::CachedHost;
	bufinfo.size = 1024 * sizeof(uint32_t);
	bufinfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	uint32_t initial[1024] = { 0xcafebabe };
	auto dummy_buffer = device.create_buffer(bufinfo, initial);

	auto events = std::async(std::launch::async, [&]()
	{
		Util::set_current_thread_name("event");
		int efd = eventfd(0, 0);

		for (int i = 0; i <= 32; i++)
		{
			kmt_fence_device_register_edge(kmt_dev, kmt_fence, i, efd);
			uint64_t v;
			read(efd, &v, sizeof(v));

			if (kmt_fence_device_query_fence(kmt_dev, kmt_fence) < uint64_t(i))
				LOGE("Signal ordering is broken.\n");
			LOGI("EventFD waited for value %u complete!\n", i);
		}

		close(efd);
	});

	auto task0 = std::async(std::launch::async, [&]()
	{
		Util::set_current_thread_name("graphics");
		Util::register_thread_index(0);
		// Every process/queue has its own monotonic timeline.
		auto tl = create_drm_timeline_from_granite(device, kmt_dev);
		uint64_t monotonic_value = 0;

		for (int i = 0; i < 16; i++)
		{
			LOGI("Graphics waiting for %u to materialize\n", 2 * i);
			// Wait API.
			uint64_t edge = kmt_fence_device_register_edge(kmt_dev, kmt_fence, 2 * i, -1);
			if (edge)
			{
				int sync_fd;
				if (!kmt_fence_device_edge_wait_materialization(kmt_dev, kmt_fence, edge, &sync_fd))
				{
					LOGE("Failed to materialize wait.\n");
					return;
				}

				auto binary_sem = device.request_semaphore_external(VK_SEMAPHORE_TYPE_BINARY, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
				ExternalHandle handle;
				handle.semaphore_handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
				handle.handle = sync_fd;
				if (!binary_sem->import_from_handle(handle))
				{
					LOGE("Failed to import binary semaphore.\n");
					return;
				}

				device.add_wait_semaphore(CommandBuffer::Type::Generic, std::move(binary_sem), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, true);
			}

			auto cmd = device.request_command_buffer(CommandBuffer::Type::Generic);
			cmd->copy_buffer(*dummy_buffer, (1 + 2 * i) * sizeof(uint32_t),
			                 *dummy_buffer, (2 * i) * sizeof(uint32_t), sizeof(uint32_t));
			device.submit(cmd);

			auto binary = device.request_timeline_semaphore_as_binary(*tl.sem, ++monotonic_value);
			device.submit_empty(CommandBuffer::Type::Generic, nullptr, binary.get());
			// Transfer the sync payload and materialize any given wait.

			LOGI("Graphics submitting signal to %u\n", 1 + 2 * i);
			kmt_fence_device_register_signal(kmt_dev, kmt_fence, tl.drm_timeline, monotonic_value, 1 + 2 * i);
		}
	});

	auto task1 = std::async(std::launch::async, [&]()
	{
		Util::set_current_thread_name("compute");
		Util::register_thread_index(1);

#if 0
		// Every process/queue has its own monotonic timeline.
		auto tl = create_drm_timeline_from_granite(device, kmt_dev);
		uint64_t monotonic_value = 0;
#endif

		for (int i = 0; i < 16; i++)
		{
			LOGI("Compute waiting for %u to materialize\n", 1 + 2 * i);
			// Wait API.
			uint64_t edge = kmt_fence_device_register_edge(kmt_dev, kmt_fence, 1 + 2 * i, -1);
			if (edge)
			{
				int sync_fd;
				if (!kmt_fence_device_edge_wait_materialization(kmt_dev, kmt_fence, edge, &sync_fd))
				{
					LOGE("Failed to materialize wait.\n");
					return;
				}

				auto binary_sem = device.request_semaphore_external(VK_SEMAPHORE_TYPE_BINARY,
				                                                    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
				ExternalHandle handle;
				handle.semaphore_handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
				handle.handle = sync_fd;
				if (!binary_sem->import_from_handle(handle))
				{
					LOGE("Failed to import binary semaphore.\n");
					return;
				}

				device.add_wait_semaphore(CommandBuffer::Type::AsyncCompute, std::move(binary_sem),
				                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, true);
			}

			auto cmd = device.request_command_buffer(CommandBuffer::Type::AsyncCompute);
			cmd->copy_buffer(*dummy_buffer, (2 + 2 * i) * sizeof(uint32_t),
			                 *dummy_buffer, (1 + 2 * i) * sizeof(uint32_t), sizeof(uint32_t));
			device.submit(cmd);

#if 0
			auto binary = device.request_timeline_semaphore_as_binary(*tl.sem, ++monotonic_value);
			device.submit_empty(CommandBuffer::Type::AsyncCompute, nullptr, binary.get());
			// Transfer the sync payload and materialize any given wait.

			LOGI("Compute submitting signal to %u\n", 2 + 2 * i);
			kmt_fence_device_register_signal(kmt_dev, kmt_fence, tl.drm_timeline, monotonic_value, 2 + 2 * i);
#else
			auto binary = device.request_semaphore_external(VK_SEMAPHORE_TYPE_BINARY, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
			device.submit_empty(CommandBuffer::Type::AsyncCompute, nullptr, binary.get());
			auto external = binary->export_to_handle();
			kmt_fence_device_register_sync_file(kmt_dev, kmt_fence, external.handle, 2 + 2 * i);
#endif
		}
	});

	events.get();
	task0.get();
	task1.get();

	auto *ptr = static_cast<const uint32_t *>(device.map_host_buffer(*dummy_buffer, MEMORY_ACCESS_READ_BIT));
	for (int i = 0; i < 34; i++)
		LOGI("Value %u = %u\n", i, ptr[i]);
}

int main()
{
	if (!Context::init_loader(nullptr))
		return EXIT_FAILURE;

	Context ctx;
	ctx.set_num_thread_indices(2);
	if (!ctx.init_instance_and_device(nullptr, 0, nullptr, 0))
		return EXIT_FAILURE;

	Device dev;
	dev.set_context(ctx);

	run_test(dev);
}