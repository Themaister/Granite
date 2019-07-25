# Granite code base overview

The top level structure contains several folders which contain high level concepts.

## Build system

The build system is pure CMake and should be very straight forward to use.
To build Granite as a standalone project for example to run the glTF viewer, it's standard CMake:

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j16 # Or whatever build system cmake spits out.
./viewer/gltf-viewer /path/to/my.gltf
```

A Python installation is probably necessary for SPIRV-Tools to build.

### Submodules

Granite uses submodules to pull in third party modules. Make sure that
`git submodule update --init` is called when checking out new versions of Granite.

### Building Android apps with Granite

There is no builtin way of building Android apps, but it's fairly straight forward to set up.
You can use the `gltf-viewer` as an example.

#### Setting up the Gradle app folder

You'll need an app folder. This is found under `viewer/app`, but you will need to make your own.
Here you place the Android manifest. The manifest needs to point to the GraniteActivity.
This activity is basically just NativeActivity with a few extra utility functions.
The lib_name needs to point to a particular native library which is the
CMake target you have chosen to use for your CMake `add_granite_application()`.
See `viewer/CMakeLists.txt` for how to use `add_granite_application()`.

See `viewer/app/AndroidManifest.xml` for an example manifest file.
`viewer/app/build.gradle` connects CMake, the manifest, where to pull assets from, etc.
Make sure that the builtin resources in assets/ are pulled in as well as your own assets.
The viewer application has its own asset folder in `viewer/assets`.
For example, to try the Android version of the `gltf-viewer`, place a scene called `scene.glb`
into `viewer/assets` and build. By default, the Android app will load from `assets://scene.glb`, which will
point to the APKs asset manager. You can place `android.json`, `config.json` and `quirks.json` into the assets folder as well.
See the code for more detail.

In `viewer/app/res`, various icons and string resources should go as normal.
The default `AndroidManifest.xml` points to a built-in Android icon, so you'll probably have to add that.

In `viewer/app/build.gradle` replace what is needed. Likely, only the `targets` line.
Otherwise, it can mostly be copy-pasted.
`viewer/build.gradle` is weird magic that has to be there.
The gradle plugin version might depend a bit on your Android Studio installation.
`viewer/settings.gradle` pulls in your app as well as the simple, common GraniteActivity Java cruft.

Once you've set it up, a normal gradle build should suffice.

### Using Granite as a dependency

Granite is designed to be used as a statically linked library, but can be built with position independent code if you need
to wrap your application in a dynamic library (e.g. libretro target).

The normal idea is to have Granite somewhere as a folder in your tree, either a submodule, symlink or whatever.
Create a CMake project and add Granite as a subdirectory.
Use `add_granite_application()` to set up the build.
It's mostly just a convenience script to link in relevant targets.

```
cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
project(AppBringup LANGUAGES CXX C)

add_subdirectory(Granite EXCLUDE_FROM_ALL)

add_granite_application(app-bringup app_bringup.cpp)
target_link_libraries(app-bringup renderer)
if (NOT ANDROID)
    target_compile_definitions(app-bringup PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\")
endif()
```

### Using Granite as a pure Vulkan backend

Some projects would only be interested in the raw Vulkan backend.
The Vulkan backend has some dependencies on other Granite modules which might end up unwieldy.
Most of these can be stripped out, e.g.:

```
# If using Granite as part of your shared library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(GRANITE_POSITION_INDEPENDENT ON CACHE STRING "Granite position independent" FORCE)
set(GRANITE_HIDDEN ON CACHE STRING "Granite hidden" FORCE)

# Disable dependencies on other modules.
set(GRANITE_VULKAN_FOSSILIZE OFF CACHE STRING "Vulkan Fossilize" FORCE)
set(GRANITE_VULKAN_MT OFF CACHE STRING "Vulkan MT" FORCE)
set(GRANITE_VULKAN_FILESYSTEM OFF CACHE STRING "Vulkan filesystem" FORCE)
add_subdirectory(Granite EXCLUDE_FROM_ALL)
```

At this point, it should only depend on SPIRV-Cross and volk.

## `application/`

This module implements anything related to an applications lifecycle.
Platform specific application things go in here as well, like various backends for `VkSurfaceKHR`,
and input handling.

- Android
- GLFW
- Headless (no WSI, used for benchmarking)
- VK_KHR_display
- libretro
- Custom plugin surface (used if you have some fancy, special purpose surface code).

This module defines the "Application" interface, used by the platform code in the main loop.
The platform code implements main() or whatever equivalent the platform requires.

The gltf-viewer application is also implemented here by `SceneViewerApplication`.
This application can load glTF scenes, and you can move around, add lights, I use it as a sandbox for testing stuff.

See `application.cpp` for how to implement a `Granite::Application` interface.
You basically implement a `render_frame()` callback.

Applications are expected to implement `Granite::application_create()`, which should create
an instance of the application, and the platform code will pump this through.
Applications do not own their main loop as some platforms do not support that.

It is perfectly possible to avoid this and just use the Vulkan backend API directly.
This is useful for standalone tooling which needs to use the GPU.
See `tools/convert_cube_to_environment.cpp` as an example here.

## `assets/`

Here the builtin assets for Granite are found. Various shaders for the most part and a couple of look-up textures.
These are not relevant if you only use Granite for its Vulkan backend.

## `compiler/`

Here `third_party/shaderc` is used to implement GLSL -> SPIR-V compilation in run-time,
with or without optimizations with SPIRV-Tools-opt.
I do this for convenience sake.
A production application will probably want to pre-compile shaders and ship the SPIR-V only.

## `event/`

Here you find the event manager. The event manager is global, and is used to dispatch events throughout the application in a decoupled way.
There are two kinds of events, latched and immediate. Latched events are events which continue to persist until they are destroyed.
This is used for things like "device created" and "swapchain created". The device remains alive until it dies, similar with the swapchain.
For new event handler registrations, the event manager will make sure to fire events for device created events even if the device created event has already been fired.
This allows subsystems to always know when the Vulkan device is alive, or dead without having to plumb a ton of state through all the subsystems.

Immediate events are things like "key pressed", "mouse moved" etc. These events fire once and disappear.
Event handlers must be member function which inherit from `Granite::EventHandler`.
You can easily create your own events by tagging a type.

E.g.:

```
class SwapchainParameterEvent : public Granite::Event
{
public:
	GRANITE_EVENT_TYPE_DECL(SwapchainParameterEvent)
};

class TestApp : public Granite::EventHandler
{
    TestApp()
    {
        EVENT_MANAGER_REGISTER_LATCH(TestApp, on_swapchain_created, on_swapchain_destroyed, SwapchainParameterEvent);
        EVENT_MANAGER_REGISTER(TestApp, on_key_pressed, KeyboardEvent);
    }
    
    // return true to keep responding to these events, false to detach yourself.
    bool on_key_pressed(const KeyboardEvent &e);
    
    void on_swapchain_created(const SwapchainParameterEvent &e);
    void on_swapchain_destroyed(const SwapchainParameterEvent &);
};
```

This module also implements a simple Entity Component System (ECS). (Not sure why I moved it to event/, but anyways).
This is used by the scene graph for various purposes. Some pretty funky variadic template magic happens here.
You create entities, and components are added to them. You can query for component groups, e.g. "give me all renderables which should be rendered opaque", things like that.
It's a very neat system for different kind of queries.
It's likely very unoptimized compared to what it could be,
but I haven't had any issues except that removing components is quite costly.

## `filesystem/`

This implements a filesystem.
The design here is a bit radical. The only file operations supported are `mmap()` or `MapViewOfFile()`. No stream API exists.
On Android, the APK buffer mapping feature is used (which should basically map to mmap anyways for uncompressed files).

The path system is based around protocols where a path is `protocol://path/inside/protocol`. The builtin protocols are:
- `builtin://`. Should always refer to `assets/` in some shape or form, where you can find the Granite builtin shaders and so on.
- `assets://`. Application specific assets. Usually set up by the application manually.
- `cache://`. Used for various caching purposes internally. Usually just points to `build_directory/cache`.

Paths without a protocol are thought to be of the `file://` protocol, which is basically the same as just using the normal filesystem
path system directly.

The filesystem supports notions of notifications when anything changes. There is a backend for inotify on Linux.
This is used to automatically reload textures and shaders when they are modified on disk. Very useful for hacking.

The protocols are set up automatically on startup, but can be overridden by the application.

## `math/`

A simple math library cloning the parts of GLM I need. It used to be GLM, but it got bogged down in very long compile times,
so I rewrote only what I needed and sped up compilation by ~40/50% o_O.

Matrix math conventions are like GLM:
- Matrices are column major. Indexing into a matrix picks out columns.
- Quaternions are declared in order W, X, Y, Z.
- The coordinate system is GL-style view space, a right handed coordinate system. In view space +X is right, +Y is up, camera points towards -Z.
- Clip space is Vulkan-like (duh). X is right, Y is down (top-left, not bottom-left like GL!), Z = 1 is far plane, Z = 0 is near plane.
  Basically the projection matrices are like GL, except Y is flipped. I could have gone for bottom-left clip space,
  but I would require VK_KHR_maintenance1 which isn't supported on some old Android phones (yay ...).

## `network/`

Some Linux-only networking code lying around.
It is used by the networked filesystem code, but it hasn't been worked on for a long time.

## `renderer/`

The biggest module. This module is responsible for implementing the high level rendering
code which deals with the problem of meshes, materials, scene graphs, lights, etc, etc.

### `renderer/fft/`

A Vulkan port of my GLFFT library. Used by Ocean rendering.

### `renderer/lights/`

Light rendering, including a clustered shading system.

### `renderer/post/`

Post-FX. HDR bloom and tonemapping, FXAA, SMAA and TAA.

### `renderer/utils/`

Some simple utility functions, like converting cubes and equirect into IBL and so on.
It can also read back and save textures (GTX format) to disk, even for arrays, cubes, etc.

### `abstract_renderable.hpp`

Renderable objects inherit from this. The `Renderer` will ask the `AbstractRenderable` for rendering info, which is pushed into a `RenderQueue`.
See `mesh.cpp` for the `StaticMesh` implementation.

### `render_components.hpp`

Some component types used in the renderer, like `OpaqueComponent`, `RenderableComponent`, etc.
Applications will pull out the components which are relevant and act on all of entities which have
certain combinations of components.

### The flow of rendering objects

You should study `SceneViewerApplication` for details.
The architecture for rendering glTF scenes is roughly as follows:

#### Setup

- Load scene with `SceneLoader`. This gives you a `Scene` which is basically an ECS paired with a node hierarchy.
- Have a `RenderContext`. This is responsible for containing global camera information, view and projection matrices,
  frustum planes (for culling) and lighting setup.
- Set up a `LightingParameters` struct. Fill in relevant data, and pass it to `RenderContext::set_lighting_parameters()`.
- Make some Camera, either an `FPSCamera` or pick a camera from the `Scene`.
- Set up a `Renderer` with a constructor argument depending if it's a forward, deferred or lighting renderer.
  If using forward, you need to set options using `Renderer::set_mesh_renderer_options_from_lighting()`.
  This makes sure that the shaders will support all the features required from the Lighting setup.

#### Per-frame

You can modify `Scene::Node` transforms every frame for say, animation.
Every frame you need to call `Scene::update_cached_transforms()`. This will walk through the node hierarchy and update
world space `AABB`, world model matrix as well as normal matrices, or the transforms for all bones for skinned meshes.

Also, we need to update the `RenderContext` based on the Camera. `RenderContext::set_camera()` will do this.

Keep a `VisibilityList` around.
Query the `Scene` for renderables. For forward rendering, you could do something like:
- `Scene::gather_visible_opaque_renderables()`
- `Scene::gather_visible_transparent_renderables()`
- `Scene::gather_unbounded_renderables()`

You pass in the render context and a visibility list, and out comes all the objects you need to render.

To actually render, you would do:
- `Renderer::begin()`: Resets render queues.
- `Renderer::push_renderables()`: Calls `AbstractRenderable::get_render_info()` to push data into the render queue.
For depth-only rendering, use `push_depth_renderables` to get simpler rendering.
- `Renderer::flush()`: Sorts the queue as appropriate, batches, and submits commands to `CommandBuffer`.
You will need to be in a render pass (see Vulkan section) to call `flush()`.

It is possible to pass in various flags to `flush()` which controls some common render state.

### Render graph

A powerful system for declaring the rendering you're doing up front, and have the render graph sort out dependencies and synchronization.
Used by the `SceneViewerApplication`.

### Scene and scene loader

Loads glTF scene, and constructs a `Scene` from it. The scene contains a node hierarchy as well as an Entity Component System to let application query relevant object types.

### Shader suite

The renderer uses a bank of shaders which renderable objects will pull shaders from. To get alternative shaders for glTF meshes and other renderables, this could be modified.

## `scene_formats/`

Deals with glTF file format import and export as well as dealing with compressed textures.

- BC1/3/6/7 is supported through ISPC
- BC 4/5 is supported by custom code
- ASTC is supported by ISPC or astcenc

A special purpose texture file format is defined here as well (GTX, Granite Texture Format, totally not confusing to anyone :P).
It's designed to be loaded directly as a memory mapped file and integrates nicely with the filesystem code.

This used to use GLI to load KTX files, but there was no clean way to pipe it through with `mmap()`
and compile times soared.

## `tests/`

A couple ad-hoc tests and sandboxy code to test things quick and dirty.
You can find some API usage examples here.

## `third_party/`

Submodule heaven. All third party submodules are checked out here. See README.md for which submodules are used.

## `threading/`

A fairly straight forward thread pool with task scheduling and dependency tracking. Not used that extensively yet.
Used for texture loading in the texture manager and for the CPU clustering implementation.
The idea is to make the render graph automatically thread everything through this, but that's a pretty large TODO.

## `tools/`

Some useful tools for special purposes. Read source for more details.

### `aa-bench`

Benchmarks AA implementations.

### `sweep_*.py`

Benchmarks and analyzes a glTF scene rendered in many different ways.

### `gltf-repacker`

Optimizes a glTF scene.
Removes duplicate mesh data, quantizes attributes, optimizes meshes, compresses textures (into GTX format), etc.

### `obj-to-gltf`

A crude converter from OBJ to glTF. Needed a special purpose converter once for Sponza.

## `ui/`

A bare-bones retained mode UI experiment.

## `util/`

Various standalone utility classes.
The most widely used one is the `IntrusivePtr` and `IntrusiveList`, used by the Vulkan backend
to deal with ref-counted handles.

The object pool is also used extensively. Pairing object pool with intrusive pointer was found
to be very nifty indeed.

## `viewer/`

A standalone glTF viewer. It uses `SceneViewerApplication` directly.

## `vulkan/`

This is the Vulkan backend and is the part of the code base which needs most
explanation and rationale behind its design.

### `context.cpp` and `context.hpp`

Here we have the "context". This is the module which:

- Initializes the Vulkan loader
- Creates a VkInstance
- Creates a VkDevice
- Initializes various VkQueues.
The goal is to find 3 queues.
One for graphics or "general" workloads,
one compute queue (ideally async compute in a different queue family),
and one transfer/DMA queue (ideally a separate DMA engine).

The queues will alias each other if no "ideal" queue can be found.
The context owns the lifetime for a VkInstance or VkDevice.

Validation layers are also hooked up and reported here.

### `wsi.cpp` and `wsi.hpp`

In the layer below we have the WSI or windowing system integration.
This module is responsible for managing `VkSurfaceKHR` and `VkSwapchainKHR`.
Surfaces are created and interacted with through the `WSIPlatform`
interface which the platform code (or application code) is responsible for implementing.

The module also has support for external swapchains, i.e. a swapchain whose images do not belong to a `VkSwapchainKHR`.
This is is useful for applications which want to use offscreen rendering only.
In this case, the implementation can simply pretend it's working with swapchain images
and provide release semaphores and take acquire semaphores directly from the application.

The WSI owns a `Device` instance as well, so `WSI` is essentially a superset of `Device`.
Normally, an `Application` instance owns a `WSI` instance as well,
so applications normally don't need to know about this.

The main entry points into WSI which are called on a per-frame basis is:
- `WSI::begin_frame()`: Acquires the swapchain, and calls `Device::begin_frame()`
- `WSI::end_frame()`: Flushes the frame with `Device::end_frame()`. If the swapchain was rendered into,
`vkQueuePresentKHR` will be called and the backbuffer is flipped on-screen. If the swapchain was not rendered into,
the implementation will stall with `vkDeviceWaitIdle` as it assumes the application is in some
kind of loading scenario and old resources should be flushed out.

This is typically called by the platform code via `Application::run_frame()`.

### `device.cpp` and `device.hpp`

This is the main interface to Vulkan, `Vulkan::Device`. Here you:
- Create and allocate resources
- Request command buffers
- Submit command buffers and signal fences and/or semaphores
- Wait for semaphores, etc
- Various physical device queries (like format support, etc)
- Interact with texture and shader managers

#### Per-frame resources

To manage synchronization between CPU and GPU at a higher level, the implementation
has a concept of frames. Each frame corresponds to a swapchain frame.
Each frame owns a data structure which serves as a pool of various resources to be deleted or recycled.

If you allocate command buffers, it comes from the pool of the current frame.
If you delete a resource like a buffer or image, it will be recycled back into
the per-frame pool, and deleted later.

Each frame has a list of fences to wait for and/or recycle when that frame index begins.
When a particular frame has begun (usually pumped through by WSI acquire), fences are waited on,
and deferred actions happen. This will be resetting command pools, descriptor set allocator state updated,
buffers and images deleted, fences and semaphores recycled, and so on.
This scheme means we avoid having to think too hard about waiting on GPU to complete stuff
before we touch them on CPU.

#### Command buffer requests and submissions

To submit work to the GPU, you can request a command buffer with a specific type:
- Generic
- Async compute
- Async graphics (tries to run graphics on the compute queue if it can support it, very special purpose)
- Transfer

This maps directly to a `VkCommandBuffer`, but has a lot of plumbing around it to make it easy.
There is a separate section for the details.

When you request a command buffer you lock the current frame, and it cannot end until you submit
the command buffer. The reasoning for this is multi-threading complicated things
and the fact that command buffers are owned by a single `VkCommandPool`,
and thus allocators which are tied to a particular frame.
I chose not to decouple command buffer allocation from the frame,
or we would end up with a huge number of separate pools,
which on some implementations would be pretty terrible.

Locking is only a potential problem with multi-threading if we try to record
some command buffer completely asynchronously with the swapchain and that operation takes a long time,
but recording the actual command buffer should be a quick and easy operation.
If locking ever becomes a problem, this needs to be redesigned a bit.
The main case where this "async command buffer" recording happens is threaded image uploads which are done by the texture manager.
However, just recording a simple `copy_buffer_to_image()` isn't the most expensive thing in the world.

#### Resources

Resources which are held by the application are managed through a "smart pointer",
but it is effectively typedef-ed away. All resource types like Buffer, Image,
etc implement `IntrusivePtrEnabled`, which embeds a shared_ptr-like control block internally.
The intent here is that we can easily make it single-threaded ref-count or multi-threaded (atomics),
by flipping a define or by changing the IntrusivePtrEnabled inheritance.

Resource handle memory is managed through an `ObjectPool`,
which means freeing and allocating objects should minimize the required heap allocations.
When the `IntrusivePtr` is destroyed, it recycles itself properly into the respective pools.

#### Allocating GPU memory

Allocating GPU memory is done by a custom heap memory allocator.
This predates the de-factor memory allocator from AMD by a long while, but it's basically the same concept.
To the host side, you have some choices when you allocate buffers and images.

Host-visible memory is always persistently mapped with `vkMapMemory()`.
Mapping and unmapping in the API is basically free,
except maybe `vkFlushMappedRanges` and similar if you're using incoherent memory.

For buffers you can decide between:
- Device: The buffer will be kept in `DEVICE_LOCAL` memory.
It may or may not be `HOST_VISIBLE`.
- Host: The buffer will be kept in `HOST_VISIBLE | HOST_COHERENT`.
Designed for upload to the GPU, because it's likely not `CACHED`.
- CachedHost: `HOST_VISIBLE | HOST_CACHED`. May not be `COHERENT`,
but the details here are abstracted through `Device::map_host_buffer()`.
Use it for readbacks from GPU to CPU.

For images you can pick:
- Physical: Backed by physical memory.
- Transient: Only backed by on-chip tile memory. Use for g-buffers, etc, although there is a simpler interface
for requesting transient surfaces, see `Device::get_transient_attachment()`.

E.g.:
```
CommandBufferHandle cmd = device->request_command_buffer();
cmd->set_texture(set = 0, binding = 1, view = my_texture->get_view(), sampler = StockSampler::LinearClamp);
```

#### Shaders

Shader objects can be requested from the Device by providing a SPIR-V blob.
Granite will manage these internally and build reflection information using SPIRV-Cross.
Programs are linked together using multiple Shader objects, this creates the final pipeline layout,
and we set up descriptor set allocators based on the associated descriptor set layouts.

#### Texture and shader managers

If filesystem support is built in (default), the Granite device also supports
a texture and shader manager. These allow you to pass in paths, and get handles back.
Through the magic of inotify, the backing shaders will be recompiled and textures will automatically update themselves.
The texture manager loads images in the background in the thread pool.

#### Submitting command buffers, signalling sync objects and waiting

You can submit command buffers using `Device::submit()`.
This will queue up submissions internally until flushed, and submit it as a batch.
You can pass in either a pointer to `Fence`, and/or `Semaphore`.
These map directly to `VkFence` and `VkSemaphore` respectively.
If you signal a fence or semaphore,
there is an implicit flush to ensure that we don't end up in a wait-before-signal scenario.

Fences can wait on CPU, while semaphores can be waited on using `Device::add_wait_semaphore()` in a particular queue.
Note that semaphores can only be waited on once (Vulkan restriction), unlike fences.

You can manually flush using `Device::flush_frame()`, wait idle and reclaim all pending resources with `Device::wait_idle()`,
or only signal fence or semaphores using `Device::submit_empty()`.

### `command_buffer.hpp` and `command_buffer.cpp`

The aim of Granite is a "mid-level" abstraction. Some convenience is allowed at the cost of CPU cycles,
but not so much that we're back to GL levels of silliness.

#### Barriers and image layouts

Granite does not attempt to perform any synchronization on behalf of the application,
except for a few isolated cases like `create_buffer()` and `create_image()`,
where we can just wait on `VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT`, and block all possible consumers of the resource.
Another exception is rendering to the swap chain.
The backend will wait for the acquire semaphore and deal with layout transitions automatically.
The other exception is transient images, these are synchronized with `VK_SUBPASS_EXTERNAL` dependencies,
and implicit layout transitions in a render pass. Applications do not need to synchronize this.

It is your responsibility to synchronize with pipeline barriers, events, or semaphores.
You will also need to change image layouts manually.

The render graph is designed to remove most of the need to do manual synchronization like this.

Image handles can have one of two layouts "Optimal" and "General". If the image is in Optimal, the command buffer recorder
will always assume that the image is being used in its related, optimal layout. E.g., when sampling, it must be
`SHADER_READ_ONLY_OPTIMAL`. In General, the image layout is assumed to always be the catch-all `VK_IMAGE_LAYOUT_GENERAL`.

#### Descriptor set management

Descriptor sets are managed internally.
Granite uses a more traditional binding model where you bind resources to slots like in GL/D3D11, except,
to be more Vulkan-like, descriptor sets and bindings are separated. And there is no remapping of bindings.
On draw call time, Granite will build new descriptor sets for you, or reuse them if it has seen it before.
The application is freed from the burden of building descriptor sets by hand.
Descriptor set memory is managed internally as well, and recycled as appropriate.

#### Pipeline management

Granite also uses a more classic way of setting rendering state.
State can be saved and restored in a more stack-like way which removes most need to set specific rendering state
all the time in scene rendering. Pipelines are compiled on-demand.

Since on-demand pipeline creation can cause issues, Granite supports pipeline caches, as well as Fossilize, which
allows us to prewarm the internal hashmaps with VkPipelines ready to go if we so choose.

#### Allocating scratch data (VBO, IBO, UBO)

Sometimes you just need to stream out data and forget about it, like vertex buffers, index buffers, and in particular,
uniform buffer data. To avoid having to manage this memory explicitly, the command buffers has convenience functions
to allocate and bind. This allocation is basically free because it's backed by a pool of linear allocators.
Always use this for streamed data which can be discarded after you're done rendering.

#### Updating textures asynchronously

Similar to creating staging data for VBO, IBO and UBOs, you can do similar kind of updates to textures.
It will allocate staging data for you, issue `vkCmdCopyBufferToImage` commands and give you a pointer you can write.

#### Drawing

Granite supports the basic draw commands you'd expect.
On draw, any dirty state or dirty descriptor sets are resolved,
just like you would expect in an older engine.

#### Render passes

Granite has very explicit render passes, and maps almost 1:1 to Vulkan.
You fill in a `RenderPassInfo` and call `CommandBuffer::begin_render_pass()` and `end_render_pass()`.
You can declare a full multipass setup, but you are freed from the burden of figuring out subpass dependencies,
layout transitions in the render passes, etc.

For the simple non-multipass case, you need to set up:
- Color attachments w/ count
- Depth stencil attachment
- Which attachments should be cleared, and to what color.
- Which attachments should be loaded to tile.
- Which attachments should be stored and not discarded.
- Whether depth-stencil is read-only.

##### Rendering to swap chain

There is a special purpose function to render to the swap chain.
Use `Device::get_swapchain_render_pass()`.
You can pick if you want color-only, color/depth or color/depth/stencil attachments.
