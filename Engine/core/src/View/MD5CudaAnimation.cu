#include <stdio.h>
#include <stdlib.h>

#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <device_functions.h>

#include <thrust/host_vector.h>
#include <thrust/device_vector.h>

#define CUDA_VERSION 8000  // GLM works with ver higher than 8.0
#define GLM_FORCE_CUDA
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  /// coerce the perspective projection matrix to be in depth: [0.0 to 1.0]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "MD5CudaAnimation.h_cu"

namespace md5_cuda_animation {
#ifdef __CUDACC__
struct __align__(16) MD5Vertex
#else
struct alignas(16) MD5Vertex
#endif
{
    // gpu data
    uint32_t gpuVertexIndex;
    // for internal using
    int startWeight;
    int weightCount;
};

#ifdef __CUDACC__
struct __align__(16) Joint
#else
struct alignas(16) Joint
#endif
{
    int parentID;

    glm::vec3 pos;
    glm::quat orientation;
};

#ifdef __CUDACC__
struct __align__(16) BoundingBox
#else
struct alignas(16) BoundingBox
#endif
{
    glm::vec3 min;
    glm::vec3 max;
};


#ifdef __CUDACC__
    struct __align__(16) FrameData
#else
    struct alignas(16) FrameData
#endif
{
    int frameID;
    float* frameData;
    uint32_t frameDataCount;
};

#ifdef __CUDACC__
    struct __align__(16) AnimJointInfo
#else
    struct alignas(16) AnimJointInfo
#endif
{
    int parentID;

    int flags;
    int startIndex;
};

#ifdef __CUDACC__
    struct __align__(16) ModelAnimation
#else
    struct alignas(16) ModelAnimation
#endif
{
    int numFrames;
    int numJoints;
    int frameRate;
    int numAnimatedComponents;

    float frameTime;
    float totalAnimTime;
    float currAnimTime;

    AnimJointInfo* jointInfo;
    uint32_t jointInfoCount;
    BoundingBox* frameBounds;
    uint32_t frameBoundsCount;
    Joint* baseFrameJoints;
    uint32_t baseFrameJointsCount;
    FrameData* frameData;
    uint32_t frameDataCount;
    Joint** frameSkeleton;
    uint32_t frameSkeletonCount;
};

#ifdef __CUDACC__
    struct __align__(16) Weight
#else
    struct alignas(16) Weight
#endif
{
    int jointID;
    float bias;
    glm::vec3 pos;
    glm::vec3 normal;
};

#ifdef __CUDACC__
    struct __align__(16) ModelSubset
#else
    struct alignas(16) ModelSubset
#endif
{
    int numTriangles;
    uint32_t realMaterialId{0u};
    uint32_t indexOffset{0u};
    uint32_t vertOffset{0u};

    VertexData* gpuVertices;
    uint32_t gpuVerticesCount;
    MD5Vertex* vertices;
    uint32_t verticesCount;
    uint32_t* indices;
    uint32_t indicesCount;
    Weight* weights;
    uint32_t weightsCount;
};

#ifdef __CUDACC__
    struct __align__(16) Model3D
#else
    struct alignas(16) Model3D
#endif
{
    Joint* joints;
    uint32_t numJoints;
    ModelSubset* subsets;
    uint32_t numSubsets;
    ModelAnimation* animations;
    uint32_t numAnimations;
    float animationSpeedMultiplier;
    float vertexMagnitudeMultiplier;
    bool isSwapYZNeeded;
    uint32_t vertBytes;   // Size of the vertex type in bytes
    };
}  // namespace md5_cuda_animation

#ifndef defined(MD5_CUDA_VERBOSE_LOG)
    constexpr bool gpu_debug_enabled = true;
#else
    constexpr bool gpu_debug_enabled = false;    
#endif

__device__  __constant__ bool GPU_DEBUG_ENABLED;

//  Error checking macro
#define cudaCheckError(ans)                   \
    {                                         \
        gpuAssert((ans), __FILE__, __LINE__); \
    }
inline void gpuAssert(cudaError_t code, const char* file, int line, bool abort = true) {
    if (code != cudaSuccess) {
        fprintf(stderr, "GPUassert: %s %s %d\n", cudaGetErrorString(code), file, line);
        if (abort)
            exit(code);
    }
}

#define gpuKernelCheck()                     \
    {                                        \
        gpuKernelAssert(__FILE__, __LINE__); \
    }
inline void gpuKernelAssert(const char* file, int line, bool abort = true) {
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "Kernel launch failed: %s %s %d\n", cudaGetErrorString(err), file, line);
        if (abort)
            exit(err);
    }
}

namespace cuda {
// Find the GPU which is selected by Vulkan and supports CUDA
int getCudaDevice(uint8_t* vkDeviceUUID, size_t UUID_SIZE) {
    constexpr int INVALID_CUDA_DEVICE = -1;
    int current_device = 0;
    int device_count = 0;
    int devices_prohibited = 0;

    cudaDeviceProp deviceProp;
    cudaCheckError(cudaGetDeviceCount(&device_count));

    if (device_count == 0) {
        fprintf(stderr, "CUDA error: no devices supporting CUDA.\n");
        return INVALID_CUDA_DEVICE;
    }

    // Find the GPU which is selected by Vulkan
    while (current_device < device_count) {
        cudaGetDeviceProperties(&deviceProp, current_device);

        if ((deviceProp.computeMode != cudaComputeModeProhibited)) {
            // Compare the cuda device UUID with vulkan UUID
            int ret = memcmp((void*)&deviceProp.uuid, vkDeviceUUID, UUID_SIZE);
            if (ret == 0) {
                cudaCheckError(cudaSetDevice(current_device));
                cudaCheckError(cudaGetDeviceProperties(&deviceProp, current_device));
                printf("GPU Device %d: \"%s\" with compute capability %d.%d\n\n", current_device, deviceProp.name,
                       deviceProp.major, deviceProp.minor);

                return current_device;
            }
        } else {
            devices_prohibited++;
        }

        current_device++;
    }

    if (devices_prohibited == device_count) {
        fprintf(stderr,
                "CUDA error:"
                " No Vulkan-CUDA Interop capable GPU found.\n");
        return INVALID_CUDA_DEVICE;
    }

    return INVALID_CUDA_DEVICE;
}
}  // namespace cuda

MD5CudaAnimation::MD5CudaAnimation(int cudaDevice, void* winMemHandleOfVkBufMem, uint64_t vkBufSize,
                                   void* winVkSemaphoreHandle, md5_animation::Model3D& _MD5Model, 
                                   uint64_t instancesBufferOffset, const std::vector<Instance>& instances, 
                                   float radius, bool isSwapYZNeeded, float animationSpeedMultiplier,
                                   float vertexMagnitudeMultiplier, uint64_t cuda_signalVkValue)
    : cpu_MD5Model(_MD5Model),
      cuda_signalVkValue(cuda_signalVkValue),
      cuda_instancesBufferOffset(instancesBufferOffset),
      cuda_radius(radius) {
    assert(_MD5Model.animations.size() > 0u && _MD5Model.subsets.size() > 0u &&
           _MD5Model.joints.size() > 0u);

    cudaCheckError(cudaMalloc((void**)&cuda_ViewProj, sizeof(glm::mat4)));

    // import the Vulkan buffer memory to CUDA space
    {
        cudaExternalMemory_t m_cudaExternalVKmem;

        cudaExternalMemoryHandleDesc externalMemoryHandleDesc = {};
        externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
        externalMemoryHandleDesc.size = vkBufSize;  // Size of the external memory object
        externalMemoryHandleDesc.handle.win32.handle =
            winMemHandleOfVkBufMem;  // external win32 memory handle of m_generalBufferMemory

        cudaCheckError(cudaImportExternalMemory(&m_cudaExternalVKmem, &externalMemoryHandleDesc));

        cudaExternalMemoryBufferDesc externalMemBufferDesc = {};
        externalMemBufferDesc.offset = 0;
        externalMemBufferDesc.size = vkBufSize;  // Size of the external memory buffer
        externalMemBufferDesc.flags = 0;

        cudaCheckError(cudaExternalMemoryGetMappedBuffer((void**)&cuda_extrVkMappedBuffer, m_cudaExternalVKmem, &externalMemBufferDesc));
    }

    // import the Vulkan semaphore to CUDA space
    cudaExternalSemaphore_t cudaSem;
    {
        cudaExternalSemaphoreHandleDesc externalSemaphoreHandleDesc = {};
        externalSemaphoreHandleDesc.type = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
        externalSemaphoreHandleDesc.handle.win32.handle = winVkSemaphoreHandle;
        externalSemaphoreHandleDesc.flags = 0;

        cudaCheckError(cudaImportExternalSemaphore(&cudaSem, &externalSemaphoreHandleDesc));
        cuda_semaphoreHandle = cudaSem;
    }

    cudaCheckError(cudaMemcpyToSymbol(GPU_DEBUG_ENABLED, &gpu_debug_enabled, sizeof(gpu_debug_enabled)));

    {
        cudaDeviceProp prop = {};
        cudaCheckError(cudaSetDevice(cudaDevice));
        cudaCheckError(cudaGetDeviceProperties(&prop, cudaDevice));

        cuda_warpSize = prop.warpSize;
        cudaDeviceGetAttribute(&cuda_SMs, cudaDevAttrMultiProcessorCount, cudaDevice);

        cudaCheckError(cudaStreamCreateWithFlags((cudaStream_t*)&cuda_stream, cudaStreamNonBlocking));
    }

    // Allocate device memory for the model data
    cudaCheckError(cudaMalloc((void**)&cuda_MD5Model, sizeof(md5_cuda_animation::Model3D)));

    for (size_t i = 0u; i < _MD5Model.animations.size(); ++i) {
        if (_MD5Model.animations[i].numJoints > cuda_maxJointsPerSkeleton) {
            cuda_maxJointsPerSkeleton = _MD5Model.animations[i].numJoints;
        }
    }
    // Allocate device memory for the interpolated skeleton
    cudaCheckError(cudaMalloc((void**)&cuda_interpolatedSkeleton, cuda_maxJointsPerSkeleton * sizeof(md5_cuda_animation::Joint)));

    cuda_cleanupFunctions.push_back([this]() {
        cudaCheckError(cudaFree(cuda_interpolatedSkeleton));
        cudaCheckError(cudaFree(cuda_MD5Model));
        cudaCheckError(cudaStreamDestroy((cudaStream_t)cuda_stream));
        cudaCheckError(cudaDestroyExternalSemaphore((cudaExternalSemaphore_t)cuda_semaphoreHandle));
        cudaCheckError(cudaFree(cuda_extrVkMappedBuffer));
        cudaCheckError(cudaFree(cuda_ViewProj));
    });

    md5_cuda_animation::Model3D host_MD5Model;

    host_MD5Model.numSubsets = static_cast<int>(_MD5Model.subsets.size());
    host_MD5Model.numJoints = static_cast<int>(_MD5Model.joints.size());
    host_MD5Model.numAnimations = static_cast<int>(_MD5Model.animations.size());
    host_MD5Model.joints = nullptr;
    host_MD5Model.subsets = nullptr;
    host_MD5Model.animations = nullptr;
    host_MD5Model.animationSpeedMultiplier = animationSpeedMultiplier;
    host_MD5Model.vertexMagnitudeMultiplier = vertexMagnitudeMultiplier;
    host_MD5Model.isSwapYZNeeded = isSwapYZNeeded;
    host_MD5Model.vertBytes = sizeof(VertexData);

    thrust::host_vector<md5_cuda_animation::Joint> joints(_MD5Model.joints.size());
    for (size_t i = 0u; i < _MD5Model.joints.size(); ++i) {
        joints[i] = md5_cuda_animation::Joint{_MD5Model.joints[i].parentID, _MD5Model.joints[i].pos, _MD5Model.joints[i].orientation};
    }

    // Allocate device memory for joints
    {
        size_t joints_size = joints.size() * sizeof(md5_cuda_animation::Joint);
        md5_cuda_animation::Joint* joints_device;

        cudaCheckError(cudaMalloc((void**)&joints_device, joints_size));
        cudaCheckError(cudaMemcpy(joints_device, joints.data(), joints_size, cudaMemcpyHostToDevice));
        host_MD5Model.joints = joints_device;  // Set the pointer to the device memory

        cuda_cleanupFunctions.push_back([joints_device]() {
            cudaCheckError(cudaFree(joints_device)); });
    }

    // Allocate device memory for subsets
    md5_cuda_animation::ModelSubset* subsets_device;
    size_t subset_size = sizeof(md5_cuda_animation::ModelSubset);
    size_t subsets_size = _MD5Model.subsets.size() * subset_size;
    cudaMalloc((void**)&subsets_device, subsets_size);

    cuda_cleanupFunctions.push_back([subsets_device]() {
        cudaCheckError(cudaFree(subsets_device));
    });

    thrust::host_vector<md5_cuda_animation::ModelSubset> subsets(_MD5Model.subsets.size());

    for (size_t i = 0u; i < _MD5Model.subsets.size(); ++i) {
        subsets[i] = md5_cuda_animation::ModelSubset{
            _MD5Model.subsets[i].numTriangles,
            _MD5Model.subsets[i].realMaterialId,
            _MD5Model.subsets[i].indexOffset,
            _MD5Model.subsets[i].vertOffset,
            nullptr,  // gpuVertices will be allocated later
            static_cast<uint32_t>(_MD5Model.subsets[i].gpuVertices.size()),
            nullptr,  // vertices will be allocated later
            static_cast<uint32_t>(_MD5Model.subsets[i].vertices.size()),
            nullptr,  // indices will be allocated later
            static_cast<uint32_t>(_MD5Model.subsets[i].indices.size()),
            nullptr,  // weights will be allocated later
            static_cast<uint32_t>(_MD5Model.subsets[i].weights.size())};

        // Copy vertices
        thrust::host_vector<md5_cuda_animation::MD5Vertex> vertices(_MD5Model.subsets[i].vertices.size());
        for (size_t j = 0u; j < _MD5Model.subsets[i].vertices.size(); ++j) {
            vertices[j] = md5_cuda_animation::MD5Vertex{_MD5Model.subsets[i].vertices[j].gpuVertexIndex, _MD5Model.subsets[i].vertices[j].startWeight,
                _MD5Model.subsets[i].vertices[j].weightCount};
        }

        // Copy vertices to device
        md5_cuda_animation::MD5Vertex* md5vertices_device;
        size_t md5vertices_size = vertices.size() * sizeof(md5_cuda_animation::MD5Vertex);
        cudaCheckError(cudaMalloc((void**)&md5vertices_device, md5vertices_size));
        cudaCheckError(cudaMemcpy(md5vertices_device, vertices.data(), md5vertices_size, cudaMemcpyHostToDevice));
        subsets[i].vertices = md5vertices_device; // holds real device address

        thrust::host_vector<VertexData> gpuVertices(_MD5Model.subsets[i].gpuVertices.size());
        gpuVertices.assign(_MD5Model.subsets[i].gpuVertices.begin(), _MD5Model.subsets[i].gpuVertices.end());

        // Copy gpuVertices to device
        VertexData* gpuvertices_device;
        size_t gpuvertices_size = gpuVertices.size() * sizeof(VertexData);
        cudaCheckError(cudaMalloc((void**)&gpuvertices_device, gpuvertices_size));
        cudaCheckError(cudaMemcpy(gpuvertices_device, gpuVertices.data(), gpuvertices_size, cudaMemcpyHostToDevice));
        subsets[i].gpuVertices = gpuvertices_device;  // holds real device address

        thrust::host_vector<uint32_t> indices(_MD5Model.subsets[i].indices.size());
        indices.assign(_MD5Model.subsets[i].indices.begin(), _MD5Model.subsets[i].indices.end());

        // Copy indices to device
        uint32_t* indices_device;
        size_t indices_size = indices.size() * sizeof(uint32_t);
        cudaCheckError(cudaMalloc((void**)&indices_device, indices_size));
        cudaCheckError(cudaMemcpy(indices_device, indices.data(), indices_size, cudaMemcpyHostToDevice));
        subsets[i].indices = indices_device;  // holds real device address

        thrust::host_vector<md5_cuda_animation::Weight> weights(_MD5Model.subsets[i].weights.size());
        for (size_t j = 0u; j < _MD5Model.subsets[i].weights.size(); ++j) {
            weights[j] = md5_cuda_animation::Weight{
                _MD5Model.subsets[i].weights[j].jointID, _MD5Model.subsets[i].weights[j].bias, 
                _MD5Model.subsets[i].weights[j].pos, _MD5Model.subsets[i].weights[j].normal
            };
        }

        // Copy weights to device
        md5_cuda_animation::Weight* weights_device;
        size_t weights_size = weights.size() * sizeof(md5_cuda_animation::Weight);
        cudaCheckError(cudaMalloc((void**)&weights_device, weights_size));
        cudaCheckError(cudaMemcpy(weights_device, weights.data(), weights_size, cudaMemcpyHostToDevice));
        subsets[i].weights = weights_device;  // holds real device address

        // Copy subsets[i] to device
        cudaCheckError(cudaMemcpy(&subsets_device[i], &subsets[i], subset_size, cudaMemcpyHostToDevice));
        
        cuda_cleanupFunctions.push_back([weights_device, indices_device, gpuvertices_device, md5vertices_device]() {
            cudaCheckError(cudaFree(weights_device)); 
            cudaCheckError(cudaFree(indices_device));
            cudaCheckError(cudaFree(gpuvertices_device));
            cudaCheckError(cudaFree(md5vertices_device));
        });
    }

    // Set the pointer to the device memory
    host_MD5Model.subsets = subsets_device;

    // Allocate device memory for animations
    md5_cuda_animation::ModelAnimation* animations_device;
    size_t animation_size = sizeof(md5_cuda_animation::ModelAnimation);
    size_t animations_size = _MD5Model.animations.size() * animation_size;
    cudaMalloc((void**)&animations_device, animations_size);

    cuda_cleanupFunctions.push_back([animations_device]() {
        cudaCheckError(cudaFree(animations_device));
    });

    thrust::host_vector<md5_cuda_animation::ModelAnimation> animations(_MD5Model.animations.size());

    for (size_t i = 0u; i < _MD5Model.animations.size(); ++i) {
        animations[i] = md5_cuda_animation::ModelAnimation{
            _MD5Model.animations[i].numFrames,
            _MD5Model.animations[i].numJoints,
            _MD5Model.animations[i].frameRate,
            _MD5Model.animations[i].numAnimatedComponents,
            _MD5Model.animations[i].frameTime,
            _MD5Model.animations[i].totalAnimTime,
            _MD5Model.animations[i].currAnimTime,
            nullptr,  // jointInfo will be allocated later
            static_cast<uint32_t>(_MD5Model.animations[i].jointInfo.size()),
            nullptr,  // frameBounds will be allocated later
            static_cast<uint32_t>(_MD5Model.animations[i].frameBounds.size()),
            nullptr,  // baseFrameJoints will be allocated later
            static_cast<uint32_t>(_MD5Model.animations[i].baseFrameJoints.size()),
            nullptr,  // frameData will be allocated later
            static_cast<uint32_t>(_MD5Model.animations[i].frameData.size()),
            nullptr,  // frameSkeleton will be allocated later
            static_cast<uint32_t>(_MD5Model.animations[i].frameSkeleton.size())};

        thrust::host_vector<md5_cuda_animation::AnimJointInfo> jointInfos(_MD5Model.animations[i].jointInfo.size());
        for (size_t j = 0u; j < _MD5Model.animations[i].jointInfo.size(); ++j) {
            jointInfos[j] = md5_cuda_animation::AnimJointInfo{_MD5Model.animations[i].jointInfo[j].parentID, _MD5Model.animations[i].jointInfo[j].flags,
                _MD5Model.animations[i].jointInfo[j].startIndex};
        }

        // Copy jointInfos to device
        md5_cuda_animation::AnimJointInfo* jointInfos_device;
        size_t jointInfos_size = jointInfos.size() * sizeof(md5_cuda_animation::AnimJointInfo);
        cudaCheckError(cudaMalloc((void**)&jointInfos_device, jointInfos_size));
        cudaCheckError(cudaMemcpy(jointInfos_device, jointInfos.data(), jointInfos_size, cudaMemcpyHostToDevice));
        animations[i].jointInfo = jointInfos_device;

        thrust::host_vector<md5_cuda_animation::BoundingBox> frameBounds(_MD5Model.animations[i].frameBounds.size());
        for (size_t j = 0u; j < _MD5Model.animations[i].frameBounds.size(); ++j) {
            frameBounds[j] = md5_cuda_animation::BoundingBox{_MD5Model.animations[i].frameBounds[j].min,
                                                             _MD5Model.animations[i].frameBounds[j].max};
        }
        
        // Copy frameBounds to device
        md5_cuda_animation::BoundingBox* frameBounds_device;
        size_t frameBounds_size = frameBounds.size() * sizeof(md5_cuda_animation::BoundingBox);
        cudaCheckError(cudaMalloc((void**)&frameBounds_device, frameBounds_size));
        cudaCheckError(cudaMemcpy(frameBounds_device, frameBounds.data(), frameBounds_size, cudaMemcpyHostToDevice));
        animations[i].frameBounds = frameBounds_device;

        thrust::host_vector<md5_cuda_animation::Joint> baseFrameJoints(_MD5Model.animations[i].baseFrameJoints.size());
        for (size_t j = 0u; j < _MD5Model.animations[i].baseFrameJoints.size(); ++j) {
            baseFrameJoints[j] = md5_cuda_animation::Joint{_MD5Model.animations[i].baseFrameJoints[j].parentID,
                _MD5Model.animations[i].baseFrameJoints[j].pos, _MD5Model.animations[i].baseFrameJoints[j].orientation};
        }

        // Copy baseFrameJoints to device
        md5_cuda_animation::Joint* baseFrameJoints_device;
        size_t baseFrameJoints_size = baseFrameJoints.size() * sizeof(md5_cuda_animation::Joint);
        cudaCheckError(cudaMalloc((void**)&baseFrameJoints_device, baseFrameJoints_size));
        cudaCheckError(cudaMemcpy(baseFrameJoints_device, baseFrameJoints.data(), baseFrameJoints_size, cudaMemcpyHostToDevice));
        animations[i].baseFrameJoints = baseFrameJoints_device;

        thrust::host_vector<md5_cuda_animation::FrameData> frameData(_MD5Model.animations[i].frameData.size());
        for (size_t j = 0u; j < _MD5Model.animations[i].frameData.size(); ++j) {
            frameData[j] = md5_cuda_animation::FrameData{_MD5Model.animations[i].frameData[j].frameID,
                                                         _MD5Model.animations[i].frameData[j].frameData.data(),
                                                         static_cast<uint32_t>(_MD5Model.animations[i].frameData[j].frameData.size())};
        }

        // Copy frameData to device
        md5_cuda_animation::FrameData* frameData_device;
        size_t frameData_size = frameData.size() * sizeof(md5_cuda_animation::FrameData);
        cudaCheckError(cudaMalloc((void**)&frameData_device, frameData_size));
        cudaCheckError(cudaMemcpy(frameData_device, frameData.data(), frameData_size, cudaMemcpyHostToDevice));
        animations[i].frameData = frameData_device;

        // Copy frameSkeleton to device
        md5_cuda_animation::Joint** frameSkeleton_device;
        size_t frameSkeleton_size = _MD5Model.animations[i].frameSkeleton.size() * sizeof(md5_cuda_animation::Joint*);
        cudaCheckError(cudaMalloc((void**)&frameSkeleton_device, frameSkeleton_size));
        //cudaCheckError(cudaMemcpy(&(animations_device[i].frameSkeleton), frameSkeleton_device, sizeof(md5_cuda_animation::Joint**), cudaMemcpyHostToDevice));

        cuda_cleanupFunctions.push_back([jointInfos_device, frameData_device, baseFrameJoints_device, frameBounds_device]() {
            cudaCheckError(cudaFree(jointInfos_device));
            cudaCheckError(cudaFree(frameData_device));
            cudaCheckError(cudaFree(baseFrameJoints_device));
            cudaCheckError(cudaFree(frameBounds_device));
        });

        thrust::host_vector<md5_cuda_animation::Joint*> frameSkeleton(_MD5Model.animations[i].frameSkeleton.size());
        for (size_t j = 0u; j < _MD5Model.animations[i].frameSkeleton.size(); ++j) {
            thrust::host_vector<md5_cuda_animation::Joint> frameSkeletonJoints(_MD5Model.animations[i].frameSkeleton[j].size());
            for (size_t k = 0u; k < _MD5Model.animations[i].frameSkeleton[j].size(); ++k) {
                frameSkeletonJoints[k] = md5_cuda_animation::Joint{_MD5Model.animations[i].frameSkeleton[j][k].parentID,
                                                                   _MD5Model.animations[i].frameSkeleton[j][k].pos,
                                                                   _MD5Model.animations[i].frameSkeleton[j][k].orientation};
            }

            // Copy frameSkeletonJoints to device
            md5_cuda_animation::Joint* frameSkeletonJoints_device;
            size_t frameSkeletonJoints_size = frameSkeletonJoints.size() * sizeof(md5_cuda_animation::Joint);
            cudaCheckError(cudaMalloc((void**)&frameSkeletonJoints_device, frameSkeletonJoints_size));
            cudaCheckError(cudaMemcpy(frameSkeletonJoints_device, frameSkeletonJoints.data(), frameSkeletonJoints_size, cudaMemcpyHostToDevice));
            frameSkeleton[j] = frameSkeletonJoints_device;

            cuda_cleanupFunctions.push_back([frameSkeletonJoints_device]() {
                cudaCheckError(cudaFree(frameSkeletonJoints_device));
            });
        }

        cudaCheckError(cudaMemcpy(frameSkeleton_device, frameSkeleton.data(), frameSkeleton_size, cudaMemcpyHostToDevice));
        animations[i].frameSkeleton = frameSkeleton_device;

        // Copy animations[i] to device
        cudaCheckError(cudaMemcpy(&animations_device[i], &animations[i], animation_size, cudaMemcpyHostToDevice));
    }
    // Set the pointer to the device memory
    host_MD5Model.animations = animations_device;

    // Copying the host model to device except the pointers
    cudaCheckError(cudaMemcpy(cuda_MD5Model, &host_MD5Model, sizeof(md5_cuda_animation::Model3D), cudaMemcpyHostToDevice));


    // Update the device model with the initial data
    // Indices data is copied only once
    for (int32_t i = 0; i < cpu_MD5Model.numSubsets; i++) {
        auto& subset = cpu_MD5Model.subsets[i];

        cudaDeviceSynchronize();  // Wait for kernel to be idle
        
        // Update the subset's buffer
        const uint32_t indexBytes = sizeof(subset.indices[0]);
        
        const uint32_t indicesSize = indexBytes * subset.indices.size();
        
        cudaCheckError(cudaMemcpy((char*)cuda_extrVkMappedBuffer + subset.indexOffset * indexBytes, subset.indices.data(), indicesSize,
                   cudaMemcpyHostToDevice));
    }

    // Instances data initialization
    {
        cudaDeviceSynchronize();  // Wait for kernel to be idle
        const uint64_t instancesSize = sizeof(instances[0]) * instances.size();
        cudaCheckError(cudaMalloc((void**)&cuda_instances_original, instancesSize));
        cudaCheckError(cudaMalloc((void**)&cuda_instances_filtered, instancesSize));
        cudaCheckError(cudaMemcpy(cuda_instances_original, instances.data(), instancesSize, cudaMemcpyHostToDevice));
        cudaCheckError(cudaMemcpy((char*)cuda_extrVkMappedBuffer + instancesBufferOffset, cuda_instances_original, instancesSize,
                                  cudaMemcpyDeviceToDevice));
        cudaCheckError(cudaMemcpy(cuda_instances_filtered, cuda_instances_original, instancesSize, cudaMemcpyDeviceToDevice));
        cudaCheckError(cudaMalloc((void**)&cuda_instances_flags, instancesSize));
        cudaCheckError(cudaMemset(cuda_instances_flags, 0, instancesSize));

        cudaCheckError(cudaMallocHost((void**)&cuda_activeInstancesCount, sizeof(uint32_t)));   

        cuda_cleanupFunctions.push_back([this]() {
            cudaCheckError(cudaFree(cuda_instances_original));
            cudaCheckError(cudaFree(cuda_instances_filtered));
            cudaCheckError(cudaFree(cuda_instances_flags));
            cudaCheckError(cudaFree(cuda_activeInstancesCount));
        });

        cuda_numInstances = instances.size();
    }
}

__device__ void swapYandZ(glm::vec3& vertexData) {
    float temp = vertexData.y;
    vertexData.y = vertexData.z;
    vertexData.z = temp;
    vertexData.z *= -1.0f;
}

__global__ void updateAnimationChunk(md5_cuda_animation::Model3D* cuda_MD5Model,
                                     md5_cuda_animation::Joint* cuda_interpolatedSkeleton, int subsetId,
                                     char* cuda_extrVkMappedBuffer, uint64_t verticesBufferOffset) {
    // Unique thread index among all blocks
    int globalThreadIndx = threadIdx.x + blockDim.x * blockIdx.x;
    // thread index within one block
    int blockThreadXIndx = threadIdx.x + threadIdx.y * blockDim.x + threadIdx.z * blockDim.x * blockDim.y;
    /// if (GPU_DEBUG_ENABLED && globalThreadIndx == 0) {
    ///     printf("updateAnimationChunk subsetId:%d\n", subsetId);
    /// }
    if (cuda_MD5Model->numSubsets <= subsetId) {
        printf("updateAnimationChunk: subsetId is out of range\n");
        return;
    }
    md5_cuda_animation::ModelSubset& subset = cuda_MD5Model->subsets[subsetId];

    /** Note use can use cuda_MD5Model.indexBytes and cuda_MD5Model.vertBytes instead of the shared memory to calculate and sync ecery time
    __shared__ uint32_t indexBytes;
    __shared__ uint32_t vertBytes;

    // init shared data on the first thread for each SM block
    if (blockThreadXIndx == 0 && cuda_MD5Model.indexBytes == 0) {
        indexBytes = sizeof(subset.indices[0]);
        vertBytes = sizeof(subset.gpuVertices[0]);
    }

    // Synchronize threads within the warp to ensure all threads have the same indexBytes, vertBytes values
    __syncwarp();*/

    // Note: we have more indices than vertices, so we need to skip the globalThreadIndx that are out of bounds

    // Update the subset's buffer by copying i-th vertex data to the mapped buffer
    if (subset.gpuVerticesCount > globalThreadIndx) {
        glm::vec3 rotatedPoint = glm::vec3(.0f, .0f, .0f);
        md5_cuda_animation::MD5Vertex& tempVert = subset.vertices[globalThreadIndx];
        VertexData& gpuVertex = subset.gpuVertices[tempVert.gpuVertexIndex];
        gpuVertex.pos = glm::vec3(.0f, .0f, .0f);     // Make sure the vertex's pos is cleared first
        gpuVertex.normal = glm::vec3(.0f, .0f, .0f);  // Clear vertices normal

        // Sum up the joints and weights information to get vertex's position and normal
        for (uint32_t j = 0; j < tempVert.weightCount; ++j) {
            const md5_cuda_animation::Weight& tempWeight = subset.weights[tempVert.startWeight + j];
            const md5_cuda_animation::Joint& tempJoint = cuda_interpolatedSkeleton[tempWeight.jointID];

            // Calculate vertex position (in joint space, eg. rotate the point around (0,0,0)) for this weight using the joint
            // orientation quaternion and its conjugate We can rotate a point using a quaternion with the equation
            // "rotatedPoint = quaternion * point * quaternionConjugate" but conjugate id nor actual for glm since it has
            // internal optimization
            rotatedPoint = tempJoint.orientation * tempWeight.pos;

            // Now move the verices position from joint space (0,0,0) to the joints position in world space, taking the
            // weights bias into account
            gpuVertex.pos += (tempJoint.pos + rotatedPoint) * tempWeight.bias;

            // Compute the normals for this frames skeleton using the weight normals from before
            // We can comput the normals the same way we compute the vertices position, only we don't have to translate them
            // (just rotate)
            rotatedPoint = tempJoint.orientation * tempWeight.normal;

            // Add to vertices normal and take weight bias into account
            gpuVertex.normal = gpuVertex.normal + (rotatedPoint * tempWeight.bias);
        }

        gpuVertex.pos *= cuda_MD5Model->vertexMagnitudeMultiplier;

        gpuVertex.normal = glm::normalize(gpuVertex.normal);

        if (cuda_MD5Model->isSwapYZNeeded) {
            swapYandZ(gpuVertex.pos);
            swapYandZ(gpuVertex.normal);
        }

        memcpy(cuda_extrVkMappedBuffer + verticesBufferOffset + (subset.vertOffset + globalThreadIndx) * cuda_MD5Model->vertBytes,
               &subset.gpuVertices[globalThreadIndx], cuda_MD5Model->vertBytes);
    } 
    
    // Note: we don't need to update indices every time, since they are static and already copied to the mapped buffer
    // Update the subset's buffer by copying i-th index to the mapped buffer
    /*if (globalThreadIndx < subset.indicesCount) {
        memcpy(cuda_extrVkMappedBuffer + (subset.indexOffset + globalThreadIndx) * cuda_MD5Model->indexBytes,
               &subset.indices[globalThreadIndx], cuda_MD5Model->indexBytes);
    }*/
}

__device__ void calculateInterpolatedSkeleton(md5_cuda_animation::Model3D* cuda_MD5Model, int animationID,
                                              md5_cuda_animation::Joint* cuda_interpolatedSkeleton, int frame0, int frame1,
                                              float interpolation) {
    // Unique thread index among all blocks
    int globalThreadIndx = threadIdx.x + blockDim.x * blockIdx.x;
    ///if (GPU_DEBUG_ENABLED && globalThreadIndx == 0) {
    ///    printf("calculateInterpolatedSkeleton animationID:%d; frame0: %d; frame1: %d; interpolation: %f\n", animationID, frame0,
    ///           frame1, interpolation);
    ///}

    md5_cuda_animation::ModelAnimation& animation = cuda_MD5Model->animations[animationID];
    ///if (GPU_DEBUG_ENABLED && globalThreadIndx == 0) {
    ///    printf("Current Model3D: numJoints: %d; numSubsets: %d; numAnimations: %d\n", cuda_MD5Model->numJoints,
    ///           cuda_MD5Model->numSubsets, cuda_MD5Model->numAnimations);
    ///}
    if (0 > animation.numJoints || animation.frameSkeletonCount <= frame0 || animation.frameSkeletonCount <= frame1) {
        printf("out of range\n");
        return;
    }

    md5_cuda_animation::Joint joint0;
    md5_cuda_animation::Joint joint1;
    if (globalThreadIndx < animation.numJoints) {
        md5_cuda_animation::Joint& tempJoint = cuda_interpolatedSkeleton[globalThreadIndx];  // Use globalThreadIndx for indexing
        joint0 = animation.frameSkeleton[frame0][globalThreadIndx];
        joint1 = animation.frameSkeleton[frame1][globalThreadIndx];

        tempJoint.parentID = joint0.parentID;  // Set the tempJoints parent id

        // Interpolate positions
        tempJoint.pos = joint0.pos + (interpolation * (joint1.pos - joint0.pos));

        // Interpolate orientations using spherical interpolation (Slerp)
        tempJoint.orientation = glm::slerp(joint0.orientation, joint1.orientation, interpolation);

        // joint updating of our interpolated skeleton completed
    }
}

__global__ void cuda_md5_update(md5_cuda_animation::Model3D* cuda_MD5Model,
                                md5_cuda_animation::Joint* cuda_interpolatedSkeleton, float deltaTimeMS, int animationID) {
    // Unique thread index among all blocks
    int globalThreadIndx = threadIdx.x + blockDim.x * blockIdx.x;
    // thread index within one block
    int blockThreadXIndx = threadIdx.x + threadIdx.y * blockDim.x + threadIdx.z * blockDim.x * blockDim.y;
    if (cuda_MD5Model->numAnimations <= animationID) {
        if (globalThreadIndx == 0)
            printf("wrong animationID: %d\n", animationID);
        return;
    }

    if (cuda_MD5Model->animations[animationID].numFrames <= 1) {
        if (globalThreadIndx == 0)
            printf("numFrames <= 1\n");
        return;
    }

    ///if (GPU_DEBUG_ENABLED && globalThreadIndx == 0) {
    ///    printf("Updating animation %d with deltaTimeMS: %f\n", animationID, deltaTimeMS);
    ///    printf("currAnimTime: %f\n", cuda_MD5Model->animations[animationID].currAnimTime);
    ///    printf("Current Model3D: numJoints: %d; numSubsets: %d; numAnimations: %d\n", 
    ///           cuda_MD5Model->numJoints, 
    ///           cuda_MD5Model->numSubsets,
    ///           cuda_MD5Model->numAnimations);
    ///}

    __shared__ float currentFrame;
    __shared__ int frame0;
    __shared__ int frame1;
    __shared__ float interpolation;
    __shared__ float currAnimTime;

    // init shared data on the first thread for each SM block
    if (blockThreadXIndx == 0) {
        currAnimTime = cuda_MD5Model->animations[animationID].currAnimTime +
                       cuda_MD5Model->animationSpeedMultiplier * deltaTimeMS / 1000.0f;  // Update the current animation time

        if (currAnimTime >= cuda_MD5Model->animations[animationID].totalAnimTime)
            currAnimTime = 0.0f;

        // Which frame are we on
        currentFrame = currAnimTime * cuda_MD5Model->animations[animationID].frameRate;
        frame0 = static_cast<int>(floorf(currentFrame));
        frame1 = frame0 + 1;

        // Make sure we don't go over the number of frames
        if (frame0 == cuda_MD5Model->animations[animationID].numFrames - 1)
            frame1 = 0;

        interpolation =
            currentFrame - frame0;  // Get the remainder (in time) between frame0 and frame1 to use as interpolation factor
    }

    // Synchronize threads within the warp to ensure all threads have the same currentFrame, frame0, frame1, and interpolation values
    __syncwarp();

    // each thread will calculate its own joint in the interpolatedSkeleton
    calculateInterpolatedSkeleton(cuda_MD5Model, animationID, cuda_interpolatedSkeleton, frame0, frame1, interpolation);

    // Synchronize threads to ensure all threads have completed the calculation of i'th joint before proceeding
    __syncthreads();

    // Update the current animation time in the model only once per animation update
    if (globalThreadIndx == 0) {
        cuda_MD5Model->animations[animationID].currAnimTime = currAnimTime;
    }

    // Print out the 10th joint of the interpolated skeleton for debugging purposes
    // check if the interpolatedSkeleton is synced for the next thread
    //if (GPU_DEBUG_ENABLED && globalThreadIndx == 1) {
    //    md5_cuda_animation::Joint& tempJoint = cuda_interpolatedSkeleton[10];
    //    printf("GPU(Cuda) InterpolatedSkeleton[10].parentID = %d; InterpolatedSkeleton[10].orientation = %f %f %f\n", tempJoint.parentID,
    //           tempJoint.orientation.x, tempJoint.orientation.y, tempJoint.orientation.z);
    //}
}

__global__ void cuda_filter_instances(uint32_t* out_activeInstancesCount, Instance* cuda_instances_original, uint32_t* cuda_instances_flags, 
                                      Instance* cuda_instances_filtered, glm::mat4* cuda_viewProj, uint32_t cuda_numInstances, char* cuda_extrVkMappedBuffer,
                                      uint64_t cuda_instancesBufferOffset, float z_far, float radius) {
    // Unique thread index among all blocks
    int globalThreadIndx = threadIdx.x + blockDim.x * blockIdx.x;
    // thread index within one block
    int blockThreadXIndx = threadIdx.x + threadIdx.y * blockDim.x + threadIdx.z * blockDim.x * blockDim.y;

    if (globalThreadIndx >= cuda_numInstances) {
        return;  // Out of bounds
    }

    glm::mat4& viewProj = *cuda_viewProj;

    __shared__ float biasValue;
    __shared__ glm::vec4 biasCubeValues[9];

    // init shared data on the first thread for each SM block
    if (blockThreadXIndx == 0) {
        biasValue = radius + 0.15f * z_far;  // to avoid choppy clipping of the model edges nearby the  camera
        biasCubeValues[0] = viewProj * glm::vec4(-biasValue, -biasValue, -biasValue, 1.0f);  // -Y
        biasCubeValues[1] = viewProj * glm::vec4(biasValue, -biasValue, -biasValue, 1.0f);
        biasCubeValues[2] = viewProj * glm::vec4(-biasValue, -biasValue, biasValue, 1.0f);
        biasCubeValues[3] = viewProj * glm::vec4(biasValue, -biasValue, biasValue, 1.0f);
        biasCubeValues[4] = viewProj * glm::vec4(-biasValue, biasValue, -biasValue, 1.0f);  // +Y
        biasCubeValues[5] = viewProj * glm::vec4(biasValue, biasValue, -biasValue, 1.0f);
        biasCubeValues[6] = viewProj * glm::vec4(-biasValue, biasValue, biasValue, 1.0f);
        biasCubeValues[7] = viewProj * glm::vec4(biasValue, biasValue, biasValue, 1.0f);
        biasCubeValues[8] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);  // no bias (for center point)
    }

    // Synchronize threads within the warp to ensure all threads have the same bias values
    __syncwarp();

    const float maxLimitVal = 1.0f + FLT_EPSILON;  // float epsilon is used to avoid precision issues
    Instance& instance = cuda_instances_original[globalThreadIndx];
    glm::vec4 clipOrig = viewProj * glm::vec4(instance.posShift, 1.0f);
    cuda_instances_flags[globalThreadIndx] = 0;
    for (const auto& bias : biasCubeValues) {
        glm::vec4 clip = clipOrig + instance.scale * bias;
        glm::vec3 ndc = glm::vec3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
        // z is in range [0, 1] for NDC, so we can check it against 0.0f and maxLimitVal
        if (glm::abs(ndc.x) <= maxLimitVal && glm::abs(ndc.y) <= maxLimitVal && ndc.z <= maxLimitVal &&
            ndc.z >= 0.0f - FLT_EPSILON) {
            cuda_instances_flags[globalThreadIndx] = 1;  // visible instance
            /** Note: we don't have to use 'break' since gpu driver can not understand what thread of warp is stopped, it affects warp overall
            * break; 
            */
        }
    }
    // Synchronize threads to ensure all threads have completed the calculation of i'th instance before proceeding
    __syncthreads();

    // Update the filtered instances buffer
    if (globalThreadIndx == 0) {
        uint32_t visibleInstances = 0;
        for (uint32_t i = 0; i < cuda_numInstances; i++) {
            if (cuda_instances_flags[i] == 1) {
                cuda_instances_filtered[visibleInstances] = cuda_instances_original[i];
                visibleInstances++;
            }
        }
        const uint64_t instancesSize = sizeof(Instance) * visibleInstances;
        memcpy((char*)cuda_extrVkMappedBuffer + cuda_instancesBufferOffset, (char*)cuda_instances_filtered, instancesSize);
        *out_activeInstancesCount = visibleInstances;  // Update the count of active instances
    }
}

uint32_t MD5CudaAnimation::update(float deltaTimeMS, int animationID, uint64_t verticesBufferOffset, const glm::mat4& viewProj,
                              float z_far) {
    assert(cuda_MD5Model != nullptr && cuda_interpolatedSkeleton != nullptr && cuda_maxJointsPerSkeleton > 0u &&
           cpu_MD5Model.animations.size() > animationID);

    cudaCheckError(cudaMemcpy(cuda_ViewProj, &viewProj[0][0], sizeof(glm::mat4), cudaMemcpyHostToDevice));

    int threadsPerBlock = cuda_warpSize;
    int blocksPerGrid = cuda_SMs;

    // Filter instances based on the view projection matrix and z_far
    if (cuda_numInstances > 1u) {
        blocksPerGrid = cuda_numInstances / threadsPerBlock + 1;
        cuda_filter_instances<<<blocksPerGrid, threadsPerBlock, 0, (cudaStream_t)cuda_stream>>>(
            cuda_activeInstancesCount, cuda_instances_original, cuda_instances_flags, cuda_instances_filtered, cuda_ViewProj,
            cuda_numInstances, cuda_extrVkMappedBuffer, cuda_instancesBufferOffset, z_far, cuda_radius);
        gpuKernelCheck();
    }

    blocksPerGrid = cpu_MD5Model.animations[animationID].numJoints / threadsPerBlock + 1;
    cuda_md5_update<<<blocksPerGrid, threadsPerBlock, 0, (cudaStream_t)cuda_stream>>>(cuda_MD5Model, cuda_interpolatedSkeleton,
                                                                                      deltaTimeMS, animationID);
    gpuKernelCheck();
    cudaDeviceSynchronize();  // Wait for cuda_interpolatedSkeleton completion before updating the subsets

    uint32_t activeInstancesCount = *cuda_activeInstancesCount;

    for (int32_t i = 0; i < cpu_MD5Model.numSubsets; i++) {
        auto& subset = cpu_MD5Model.subsets[i];
        int32_t subsetVertices = glm::max(subset.gpuVertices.size(),
                     subset.vertices.size());  // subset.indices.size() is not used since indices are copied only once
        blocksPerGrid = subsetVertices / threadsPerBlock + 1;
        updateAnimationChunk<<<blocksPerGrid, threadsPerBlock, 0, (cudaStream_t)cuda_stream>>>(
            cuda_MD5Model, cuda_interpolatedSkeleton, i, cuda_extrVkMappedBuffer, verticesBufferOffset);

        gpuKernelCheck();
    }

    // Signal vulkan to continue with the updated buffers
    cudaExternalSemaphoreSignalParams signalParams = {};
    signalParams.flags = 0;
    signalParams.params.fence.value = cuda_signalVkValue;
    cudaExternalSemaphore_t cudaSem = (cudaExternalSemaphore_t)cuda_semaphoreHandle;
    cudaCheckError(cudaSignalExternalSemaphoresAsync(&cudaSem, &signalParams, 1, (cudaStream_t)cuda_stream));
    cuda_signalVkValue++;  // increment signal value for next synchronization with next Vulkan swapchain

    return activeInstancesCount;
}
