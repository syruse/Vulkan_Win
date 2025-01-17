#include "MD5Model.h"
#include <array>
#include <cassert>
#include <fstream>
#include <future>
#include "Constants.h"
#include "PipelineCreatorTextured.h"
#include "Utils.h"

void MD5Model::init() {
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};

    if (loadMD5Model(vertices, indices) && loadMD5Anim()) {
        /// uploading verts & indices into CPU\GPU shared memory
        const VkDeviceSize indicesSize = sizeof(indices[0]) * indices.size();
        m_verticesBufferOffset = indicesSize;
        const VkDeviceSize verticesSize = sizeof(vertices[0]) * vertices.size();
        m_bufferSize = indicesSize + verticesSize;

        Utils::VulkanCreateBuffer(p_devide, m_vkState._core.getPhysDevice(), m_bufferSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_generalBuffer,
                                  m_generalBufferMemory);

        void* data;
        vkMapMemory(p_devide, m_generalBufferMemory, 0, m_bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)indicesSize);
        memcpy((char*)data + m_verticesBufferOffset, vertices.data(), verticesSize);
        vkUnmapMemory(p_devide, m_generalBufferMemory);
    } else {
        Utils::printLog(ERROR_PARAM, "Couldn't load md5 model:", m_md5ModelFileName);
    }
}

bool MD5Model::loadMD5Anim() {
    assert(!m_md5AnimFileName.empty());

    std::string absPath = Utils::formPath(Constants::MODEL_DIR, m_md5AnimFileName);

    std::ifstream fileIn(absPath.c_str());
    ModelAnimation tempAnim;

    std::string checkString;  // Stores the next string from our file

    if (fileIn) {
        while (fileIn)  // Loop until the end of the file is reached
        {
            fileIn >> checkString;  // Get next string from file

            if (checkString == "MD5Version")  // Get MD5 version (this function supports version 10)
            {
                fileIn >> checkString;
            } else if (checkString == "commandline") {
                std::getline(fileIn, checkString);  // Ignore the rest of this line
            } else if (checkString == "numFrames") {
                fileIn >> tempAnim.numFrames;  // Store number of frames in this animation
            } else if (checkString == "numJoints") {
                fileIn >> tempAnim.numJoints;  // Store number of joints (must match .md5mesh)
            } else if (checkString == "frameRate") {
                fileIn >> tempAnim.frameRate;  // Store animation's frame rate (frames per second)
            } else if (checkString == "numAnimatedComponents") {
                fileIn >> tempAnim.numAnimatedComponents;  // Number of components in each frame section
            } else if (checkString == "hierarchy") {
                fileIn >> checkString;  // Skip opening bracket "{"

                for (int i = 0; i < tempAnim.numJoints; i++)  // Load in each joint
                {
                    AnimJointInfo tempJoint;

                    fileIn >> tempJoint.name;  // Get joints name
                    // Sometimes the names might contain spaces. If that is the case, we need to continue
                    // to read the name until we get to the closing " (quotation marks)
                    if (tempJoint.name[tempJoint.name.size() - 1] != '"') {
                        char checkChar;
                        bool jointNameFound = false;
                        while (!jointNameFound) {
                            checkChar = fileIn.get();

                            if (checkChar == '"')
                                jointNameFound = true;

                            tempJoint.name += checkChar;
                        }
                    }

                    // Remove the quotation marks from joints name
                    tempJoint.name.erase(0, 1);
                    tempJoint.name.erase(tempJoint.name.size() - 1, 1);

                    fileIn >> tempJoint.parentID;    // Get joints parent ID
                    fileIn >> tempJoint.flags;       // Get flags
                    fileIn >> tempJoint.startIndex;  // Get joints start index

                    // Make sure the joint exists in the model, and the parent ID's match up
                    // because the bind pose (md5mesh) joint hierarchy and the animations (md5anim)
                    // joint hierarchy must match up
                    bool jointMatchFound = false;
                    tempAnim.jointInfo.reserve(m_MD5Model.numJoints);
                    for (int k = 0; k < m_MD5Model.numJoints; k++) {
                        if (m_MD5Model.joints[k].name == tempJoint.name) {
                            if (m_MD5Model.joints[k].parentID == tempJoint.parentID) {
                                jointMatchFound = true;
                                tempAnim.jointInfo.push_back(tempJoint);
                            }
                        }
                    }
                    if (!jointMatchFound)  // If the skeleton system does not match up, return false
                        return false;      // You might want to add an error message here

                    std::getline(fileIn, checkString);  // Skip rest of this line
                }
            } else if (checkString == "bounds")  // Load in the AABB for each animation
            {
                fileIn >> checkString;  // Skip opening bracket "{"

                tempAnim.frameBounds.reserve(tempAnim.numFrames);
                for (int i = 0; i < tempAnim.numFrames; i++) {
                    BoundingBox tempBB;

                    fileIn >> checkString;  // Skip "("
                    fileIn >> tempBB.min.x >> tempBB.min.y >> tempBB.min.z;
                    fileIn >> checkString >> checkString;  // Skip ") ("
                    fileIn >> tempBB.max.x >> tempBB.max.y >> tempBB.max.z;
                    fileIn >> checkString;  // Skip ")"

                    tempAnim.frameBounds.push_back(tempBB);
                }
            } else if (checkString == "baseframe")  // This is the default position for the animation
            {                                       // All frames will build their skeletons off this
                fileIn >> checkString;              // Skip opening bracket "{"

                tempAnim.baseFrameJoints.reserve(tempAnim.numJoints);
                for (int i = 0; i < tempAnim.numJoints; i++) {
                    Joint tempBFJ;

                    fileIn >> checkString;  // Skip "("
                    fileIn >> tempBFJ.pos.x >> tempBFJ.pos.y >> tempBFJ.pos.z;
                    fileIn >> checkString >> checkString;  // Skip ") ("
                    fileIn >> tempBFJ.orientation.x >> tempBFJ.orientation.y >> tempBFJ.orientation.z;
                    fileIn >> checkString;  // Skip ")"

                    tempAnim.baseFrameJoints.push_back(tempBFJ);
                }
            } else if (checkString ==
                       "frame")  // Load in each frames skeleton (the parts of each joint that changed from the base frame)
            {
                FrameData tempFrame;

                fileIn >> tempFrame.frameID;  // Get the frame ID

                fileIn >> checkString;  // Skip opening bracket "{"

                tempFrame.frameData.reserve(tempAnim.numAnimatedComponents);
                for (int i = 0; i < tempAnim.numAnimatedComponents; i++) {
                    float tempData;
                    fileIn >> tempData;  // Get the data

                    tempFrame.frameData.push_back(tempData);
                }

                tempAnim.frameData.push_back(tempFrame);

                ///*** build the frame skeleton ***///
                std::vector<Joint> tempSkeleton;

                tempSkeleton.reserve(tempAnim.jointInfo.size());
                for (int i = 0; i < tempAnim.jointInfo.size(); i++) {
                    int k = 0;  // Keep track of position in frameData array

                    // Start the frames joint with the base frame's joint
                    Joint tempFrameJoint = tempAnim.baseFrameJoints[i];

                    tempFrameJoint.parentID = tempAnim.jointInfo[i].parentID;

                    // Notice
                    // If you have problems with loading some models, it's possible
                    // the model was created in a left hand coordinate system. in that case, just reflip all the
                    // y and z axes in our md5 mesh and anim loader.
                    if (tempAnim.jointInfo[i].flags & 1)  // pos.x	( 000001 )
                        tempFrameJoint.pos.x = tempFrame.frameData[tempAnim.jointInfo[i].startIndex + k++];

                    if (tempAnim.jointInfo[i].flags & 2)  // pos.y	( 000010 )
                        tempFrameJoint.pos.y = tempFrame.frameData[tempAnim.jointInfo[i].startIndex + k++];

                    if (tempAnim.jointInfo[i].flags & 4)  // pos.z	( 000100 )
                        tempFrameJoint.pos.z = tempFrame.frameData[tempAnim.jointInfo[i].startIndex + k++];

                    if (tempAnim.jointInfo[i].flags & 8)  // orientation.x	( 001000 )
                        tempFrameJoint.orientation.x = tempFrame.frameData[tempAnim.jointInfo[i].startIndex + k++];

                    if (tempAnim.jointInfo[i].flags & 16)  // orientation.y	( 010000 )
                        tempFrameJoint.orientation.y = tempFrame.frameData[tempAnim.jointInfo[i].startIndex + k++];

                    if (tempAnim.jointInfo[i].flags & 32)  // orientation.z	( 100000 )
                        tempFrameJoint.orientation.z = tempFrame.frameData[tempAnim.jointInfo[i].startIndex + k++];

                    // vector to quat converssion
                    // Compute the quaternions w
                    float t = 1.0f - (tempFrameJoint.orientation.x * tempFrameJoint.orientation.x) -
                              (tempFrameJoint.orientation.y * tempFrameJoint.orientation.y) -
                              (tempFrameJoint.orientation.z * tempFrameJoint.orientation.z);
                    if (t < 0.0f) {
                        tempFrameJoint.orientation.w = 0.0f;
                    } else {
                        tempFrameJoint.orientation.w = -sqrtf(t);
                    }

                    // Now, if the upper arm of your skeleton moves, you need to also move the lower part of your arm, and then
                    // the hands, and then finally the fingers (possibly weapon or tool too) This is where joint hierarchy comes
                    // in. We start at the top of the hierarchy, and move down to each joints child, rotating and translating them
                    // based on their parents rotation and translation. We can assume that by the time we get to the child, the
                    // parent has already been rotated and transformed based of it's parent. We can assume this because the child
                    // should never come before the parent in the files we loaded in.
                    if (tempFrameJoint.parentID >= 0) {
                        Joint parentJoint = tempSkeleton[tempFrameJoint.parentID];

                        glm::vec3 rotatedPoint = parentJoint.orientation * tempFrameJoint.pos;

                        // Translate the joint to model space by adding the parent joint's pos to it
                        tempFrameJoint.pos = rotatedPoint + parentJoint.pos;

                        // Currently the joint is oriented in its parent joints space, we now need to orient it in
                        // model space by multiplying the two orientations together (parentOrientation * childOrientation) <- In
                        // that order

                        tempFrameJoint.orientation = glm::normalize(parentJoint.orientation * tempFrameJoint.orientation);
                    }

                    // Store the joint into our temporary frame skeleton
                    tempSkeleton.push_back(tempFrameJoint);
                }

                // Push back our newly created frame skeleton into the animation's frameSkeleton array
                tempAnim.frameSkeleton.push_back(tempSkeleton);

                fileIn >> checkString;  // Skip closing bracket "}"
            }
        }

        // Calculate and store some usefull animation data
        tempAnim.frameTime = 1.0f / tempAnim.frameRate;                    // Set the time per frame
        tempAnim.totalAnimTime = tempAnim.numFrames * tempAnim.frameTime;  // Set the total time the animation takes
        tempAnim.currAnimTime = 0.0f;                                      // Set the current time to zero

        m_MD5Model.animations.push_back(tempAnim);  // Push back the animation into our model object
    } else                                          // If the file was not loaded
    {
        Utils::printLog(ERROR_PARAM, "Couldn't load animation file", m_md5AnimFileName);
        return false;
    }
    return true;
}

void MD5Model::update(float deltaTimeMS, int animationID) {
    if (m_MD5Model.animations.size() <= animationID) {
        Utils::printLog(ERROR_PARAM, "wrong animationID: ", animationID);
        return;
    }
    assert(m_MD5Model.animations[animationID].numFrames > 1);

    float currentFrame{0.0f};
    std::size_t frame0{0u};
    std::size_t frame1{0u};
    float interpolation{0.0f};

    m_MD5Model.animations[animationID].currAnimTime +=
        m_animationSpeedMultiplier * deltaTimeMS / 1000.0f;  // Update the current animation time

    if (m_MD5Model.animations[animationID].currAnimTime >= m_MD5Model.animations[animationID].totalAnimTime)
        m_MD5Model.animations[animationID].currAnimTime = 0.0f;

    // Which frame are we on
    currentFrame = m_MD5Model.animations[animationID].currAnimTime * m_MD5Model.animations[animationID].frameRate;
    frame0 = static_cast<std::size_t>(floorf(currentFrame));
    frame1 = frame0 + 1u;

    // Make sure we don't go over the number of frames
    if (frame0 == m_MD5Model.animations[animationID].numFrames - 1)
        frame1 = 0;

    interpolation =
        currentFrame - frame0;  // Get the remainder (in time) between frame0 and frame1 to use as interpolation factor

    // Create a frame skeleton to store the interpolated skeletons in
    if (mInterpolatedSkeleton.size() < m_MD5Model.animations[animationID].numJoints) {
        mInterpolatedSkeleton.resize(m_MD5Model.animations[animationID].numJoints);
    }

    std::array<std::future<void>, 4u> workerThreads;
    std::size_t chunkOffset{0u};
    std::size_t indexFrom{0u};
    std::size_t indexTo{0u};
    std::size_t workerThreadIndexPlusOne{0u};

    // Compute the interpolated skeleton in worker_threads
    chunkOffset = m_MD5Model.animations[animationID].numJoints / workerThreads.size();
    for (std::size_t workerThreadIndex = 0u; workerThreadIndex < workerThreads.size(); ++workerThreadIndex) {
        workerThreadIndexPlusOne = workerThreadIndex + 1U;
        indexFrom = workerThreadIndex * chunkOffset;
        indexTo = workerThreadIndexPlusOne > workerThreads.size() ? m_MD5Model.animations[animationID].numJoints
                                                                  : workerThreadIndexPlusOne * chunkOffset;
        workerThreads[workerThreadIndex] = std::async(std::launch::async, &MD5Model::calculateInterpolatedSkeleton, this,
                                                      animationID, frame0, frame1, interpolation, indexFrom, indexTo);
    }
    for (auto& thread : workerThreads) {
        thread.wait();
    }

    // Update the subsets vertex buffer in worker_threads
    auto p_device = m_vkState._core.getDevice();
    assert(p_device);
    void* data;

    vkMapMemory(p_device, m_generalBufferMemory, 0u, m_bufferSize, 0, &data);

    // in most cases we have one single heavy subset which must be splitted for parallel calculation
    for (std::size_t k = 0u; k < m_MD5Model.numSubsets; k++) {
        chunkOffset = m_MD5Model.subsets[k].vertices.size() / workerThreads.size();
        for (std::size_t workerThreadIndex = 0u; workerThreadIndex < workerThreads.size(); ++workerThreadIndex) {
            workerThreadIndexPlusOne = workerThreadIndex + 1U;
            indexFrom = workerThreadIndex * chunkOffset;
            indexTo = workerThreadIndexPlusOne > workerThreads.size() ? m_MD5Model.subsets[k].vertices.size()
                                                                      : workerThreadIndexPlusOne * chunkOffset;
            workerThreads[workerThreadIndex] =
                std::async(std::launch::async, &MD5Model::updateAnimationChunk, this, k, indexFrom, indexTo, (char*)data);
        }

        for (auto& thread : workerThreads) {
            thread.wait();
        }
    }

    vkUnmapMemory(p_device, m_generalBufferMemory);
}

void MD5Model::calculateInterpolatedSkeleton(std::size_t animationID, std::size_t frame0, std::size_t frame1, float interpolation,
                                             std::size_t indexFrom, std::size_t indexTo) {
    ModelAnimation& animation = m_MD5Model.animations[animationID];
    assert(indexFrom < animation.numJoints && indexTo <= animation.numJoints && indexTo <= mInterpolatedSkeleton.size() &&
           animation.frameSkeleton.size() > frame0 && animation.frameSkeleton.size() > frame1);
    Joint joint0;
    Joint joint1;
    for (std::size_t i = indexFrom; i < indexTo; i++) {
        Joint& tempJoint = mInterpolatedSkeleton[i];
        joint0 = animation.frameSkeleton[frame0][i];  // Get the i'th joint of frame0's skeleton
        joint1 = animation.frameSkeleton[frame1][i];  // Get the i'th joint of frame1's skeleton

        tempJoint.parentID = joint0.parentID;  // Set the tempJoints parent id

        // Interpolate positions
        tempJoint.pos = joint0.pos + (interpolation * (joint1.pos - joint0.pos));

        // Interpolate orientations using spherical interpolation (Slerp)
        tempJoint.orientation = glm::slerp(joint0.orientation, joint1.orientation, interpolation);

        // joint updating of our interpolated skeleton completed
    }
}

void MD5Model::updateAnimationChunk(std::size_t subsetId, std::size_t indexFrom, std::size_t indexTo, char* data) {
    ModelSubset& subset = m_MD5Model.subsets[subsetId];
    assert(data && indexFrom < subset.vertices.size() && indexTo <= subset.vertices.size());

    glm::vec3 rotatedPoint = glm::vec3(.0f, .0f, .0f);
    for (std::size_t i = indexFrom; i < indexTo; ++i) {
        MD5Vertex& tempVert = subset.vertices[i];
        tempVert.gpuVertex.pos = glm::vec3(.0f, .0f, .0f);     // Make sure the vertex's pos is cleared first
        tempVert.gpuVertex.normal = glm::vec3(.0f, .0f, .0f);  // Clear vertices normal

        // Sum up the joints and weights information to get vertex's position and normal
        for (std::size_t j = 0; j < tempVert.weightCount; ++j) {
            const Weight& tempWeight = subset.weights[tempVert.startWeight + j];
            const Joint& tempJoint = mInterpolatedSkeleton[tempWeight.jointID];

            // Calculate vertex position (in joint space, eg. rotate the point around (0,0,0)) for this weight using the joint
            // orientation quaternion and its conjugate We can rotate a point using a quaternion with the equation
            // "rotatedPoint = quaternion * point * quaternionConjugate" but conjugate id nor actual for glm since it has
            // internal optimization
            rotatedPoint = tempJoint.orientation * tempWeight.pos;

            // Now move the verices position from joint space (0,0,0) to the joints position in world space, taking the
            // weights bias into account
            tempVert.gpuVertex.pos += (tempJoint.pos + rotatedPoint) * tempWeight.bias;

            // Compute the normals for this frames skeleton using the weight normals from before
            // We can comput the normals the same way we compute the vertices position, only we don't have to translate them
            // (just rotate)
            rotatedPoint = tempJoint.orientation * tempWeight.normal;

            // Add to vertices normal and take weight bias into account
            tempVert.gpuVertex.normal = tempVert.gpuVertex.normal + (rotatedPoint * tempWeight.bias);
        }

        tempVert.gpuVertex.pos *= m_vertexMagnitudeMultiplier;

        tempVert.gpuVertex.normal = glm::normalize(tempVert.gpuVertex.normal);

        if (m_isSwapYZNeeded) {
            swapYandZ(tempVert.gpuVertex.pos);
            swapYandZ(tempVert.gpuVertex.normal);
        }
    }

    // Update the subset's buffer
    static const std::size_t indexBytes = sizeof(subset.indices[0]);
    static const std::size_t vertBytes = sizeof(subset.gpuVertices[0]);

    const std::size_t indicesSize = indexBytes * subset.indices.size();
    const std::size_t verticesSize = vertBytes * subset.gpuVertices.size();

    memcpy((char*)data + subset.indexOffset * indexBytes, subset.indices.data(), indicesSize);
    memcpy((char*)data + m_verticesBufferOffset + subset.vertOffset * vertBytes, subset.gpuVertices.data(), verticesSize);
}

inline void MD5Model::swapYandZ(glm::vec3& vertexData) {
    std::swap(vertexData.y, vertexData.z);
    vertexData.z *= -1.0f;
}

bool MD5Model::loadMD5Model(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    assert(m_pipelineCreatorTextured);
    assert(!m_md5ModelFileName.empty());

    std::string absPath = Utils::formPath(Constants::MODEL_DIR, m_md5ModelFileName);

    std::ifstream fileIn(absPath.c_str());

    std::string checkString;  // Stores the next string from our file

    if (fileIn)  // Check if the file was opened
    {
        while (fileIn)  // Loop until the end of the file is reached
        {
            fileIn >> checkString;  // Get next string from file

            if (checkString == "MD5Version")  // Get MD5 version (this function supports version 10)
            {
            } else if (checkString == "commandline") {
                std::getline(fileIn, checkString);  // Ignore the rest of this line
            } else if (checkString == "numJoints") {
                fileIn >> m_MD5Model.numJoints;  // Store number of joints
                m_MD5Model.joints.reserve(m_MD5Model.numJoints);
            } else if (checkString == "numMeshes") {
                fileIn >> m_MD5Model.numSubsets;  // Store number of meshes or subsets which we will call them
                m_MD5Model.subsets.reserve(m_MD5Model.numSubsets);
            } else if (checkString == "joints") {
                Joint tempJoint;

                fileIn >> checkString;  // Skip the "{"

                for (int i = 0; i < m_MD5Model.numJoints; i++) {
                    fileIn >> tempJoint.name;  // Store joints name
                    // Sometimes the names might contain spaces. If that is the case, we need to continue
                    // to read the name until we get to the closing " (quotation marks)
                    if (tempJoint.name[tempJoint.name.size() - 1] != '"') {
                        char checkChar;
                        bool jointNameFound = false;
                        while (!jointNameFound) {
                            checkChar = fileIn.get();

                            if (checkChar == '"')
                                jointNameFound = true;

                            tempJoint.name += checkChar;
                        }
                    }

                    fileIn >> tempJoint.parentID;  // Store Parent joint's ID

                    fileIn >> checkString;  // Skip the "("

                    // Store position of this joint (swap y and z axis if model was made in RH Coord Sys)
                    fileIn >> tempJoint.pos.x >> tempJoint.pos.y >> tempJoint.pos.z;

                    fileIn >> checkString >> checkString;  // Skip the ")" and "("

                    // Store orientation of this joint
                    fileIn >> tempJoint.orientation.x >> tempJoint.orientation.y >> tempJoint.orientation.z;

                    // Remove the quotation marks from joints name
                    tempJoint.name.erase(0, 1);
                    tempJoint.name.erase(tempJoint.name.size() - 1, 1);

                    // Compute the w axis of the quaternion (The MD5 model uses a 3D vector to describe the
                    // direction the bone is facing. However, we need to turn this into a quaternion, and the way
                    // quaternions work, is the xyz values describe the axis of rotation, while the w is a value
                    // between 0 and 1 which describes the angle of rotation)
                    float t = 1.0f - (tempJoint.orientation.x * tempJoint.orientation.x) -
                              (tempJoint.orientation.y * tempJoint.orientation.y) -
                              (tempJoint.orientation.z * tempJoint.orientation.z);
                    if (t < 0.0f) {
                        tempJoint.orientation.w = 0.0f;
                    } else {
                        tempJoint.orientation.w = -sqrtf(t);
                    }

                    std::getline(fileIn, checkString);  // Skip rest of this line

                    m_MD5Model.joints.push_back(tempJoint);  // Store the joint into this models joint vector
                }

                fileIn >> checkString;  // Skip the "}"
            } else if (checkString == "mesh") {
                m_MD5Model.subsets.emplace_back();
                ModelSubset& subset = m_MD5Model.subsets.back();
                int numVerts, numTris, numWeights;

                fileIn >> checkString;  // Skip the "{"

                fileIn >> checkString;
                while (checkString != "}")  // Read until '}'
                {
                    if (checkString == "shader")  // Load the texture
                    {
                        std::string diffuse_texname;
                        fileIn >> diffuse_texname;  // Get texture's filename

                        // Take spaces into account if filename or material name has a space in it
                        if (diffuse_texname[diffuse_texname.size() - 1] != '"') {
                            char checkChar;
                            bool fileNameFound = false;
                            while (!fileNameFound) {
                                checkChar = fileIn.get();

                                if (checkChar == '"')
                                    fileNameFound = true;

                                diffuse_texname += checkChar;
                            }
                        }

                        // Remove the quotation marks from texture path
                        diffuse_texname.erase(0, 1);
                        diffuse_texname.erase(diffuse_texname.size() - 1, 1);

                        auto texture = m_textureFactory.create2DArrayTexture(std::vector<std::string>{diffuse_texname});
                        if (!texture.expired()) {
                            subset.realMaterialId = m_pipelineCreatorTextured->createDescriptor(
                                texture, m_textureFactory.getTextureSampler(texture.lock()->mipLevels));
                        } else {
                            Utils::printLog(ERROR_PARAM, "couldn't create texture", diffuse_texname);
                        }

                        std::getline(fileIn, checkString);  // Skip rest of this line
                    } else if (checkString == "numverts") {
                        fileIn >> numVerts;  // Store number of vertices

                        std::getline(fileIn, checkString);  // Skip rest of this line

                        subset.vertices.reserve(numVerts);
                        subset.gpuVertices.reserve(numVerts);
                        for (int i = 0; i < numVerts; i++) {
                            subset.gpuVertices.emplace_back();
                            I3DModel::Vertex& gpuVert = subset.gpuVertices.back();
                            MD5Vertex tempVert{gpuVert};

                            fileIn >> checkString  // Skip "vert # ("
                                >> checkString >> checkString;

                            fileIn >> gpuVert.texCoord.x  // Store tex coords
                                >> gpuVert.texCoord.y;

                            gpuVert.texCoord.y = 1.0f - gpuVert.texCoord.y;

                            fileIn >> checkString;  // Skip ")"

                            fileIn >> tempVert.startWeight;  // Index of first weight this vert will be weighted to

                            fileIn >> tempVert.weightCount;  // Number of weights for this vertex

                            std::getline(fileIn, checkString);  // Skip rest of this line

                            subset.vertices.push_back(tempVert);  // Push back this vertex into subsets vertex vector
                        }
                    } else if (checkString == "numtris") {
                        fileIn >> numTris;
                        subset.numTriangles = numTris;

                        std::getline(fileIn, checkString);  // Skip rest of this line

                        subset.indices.reserve(numTris * 3u);
                        for (int i = 0; i < numTris; i++)  // Loop through each triangle
                        {
                            uint32_t tempIndex[3];
                            fileIn >> checkString;  // Skip "tri"
                            fileIn >> checkString;  // Skip tri counter

                            for (int k = 0; k < 3; k++)  // Store the 3 indices
                            {
                                fileIn >> tempIndex[k];
                            }
                            // adding indices in backward order since our front face winding order is
                            // VK_FRONT_FACE_COUNTER_CLOCKWISE unlike DirectX
                            for (int k = 2; k >= 0; --k) {
                                subset.indices.push_back(tempIndex[k]);
                            }

                            std::getline(fileIn, checkString);  // Skip rest of this line
                        }
                    } else if (checkString == "numweights") {
                        fileIn >> numWeights;

                        std::getline(fileIn, checkString);  // Skip rest of this line

                        subset.weights.reserve(numWeights);
                        for (int i = 0; i < numWeights; i++) {
                            Weight tempWeight;
                            fileIn >> checkString >> checkString;  // Skip "weight #"

                            fileIn >> tempWeight.jointID;  // Store weight's joint ID

                            fileIn >> tempWeight.bias;  // Store weight's influence over a vertex

                            fileIn >> checkString;  // Skip "("

                            fileIn >> tempWeight.pos.x  // Store weight's pos in joint's local space
                                >> tempWeight.pos.y >> tempWeight.pos.z;

                            std::getline(fileIn, checkString);  // Skip rest of this line

                            subset.weights.push_back(tempWeight);  // Push back tempWeight into subsets Weight array
                        }

                    } else
                        std::getline(fileIn, checkString);  // Skip anything else

                    fileIn >> checkString;  // Skip "}"
                }

                //*** find each vertex's position using the joints and weights ***//
                glm::vec3 rotatedPoint = glm::vec3{0.0f, 0.0f, 0.0f};
                for (int i = 0; i < subset.vertices.size(); ++i) {
                    MD5Vertex& tempVert = subset.vertices[i];
                    tempVert.gpuVertex.pos = glm::vec3{0.0f, 0.0f, 0.0f};  // Make sure the vertex's pos is cleared first

                    // Sum up the joints and weights information to get vertex's position
                    for (int j = 0; j < tempVert.weightCount; ++j) {
                        const Weight& tempWeight = subset.weights[tempVert.startWeight + j];
                        const Joint& tempJoint = m_MD5Model.joints[tempWeight.jointID];

                        // Calculate vertex position (in joint space, eg. rotate the point using joint orientation quaternion)
                        rotatedPoint = tempJoint.orientation * tempWeight.pos;

                        // Now move the verices position from joint space (0,0,0) to the joints position in world space, taking
                        // the weights bias into account The weight bias is used because multiple weights might have an effect on
                        // the vertices final position. Each weight is attached to one joint.
                        tempVert.gpuVertex.pos += (tempJoint.pos + rotatedPoint) * tempWeight.bias;

                        // Basically what has happened above, is we have taken the weights position relative to the joints
                        // position we then rotate the weights position (so that the weight is actually being rotated around (0,
                        // 0, 0) in world space) using the quaternion describing the joints rotation. We have stored this rotated
                        // point in rotatedPoint, which we then add to the joints position (because we rotated the weight's
                        // position around (0,0,0) in world space, and now need to translate it so that it appears to have been
                        // rotated around the joints position). Finally we multiply the answer with the weights bias, or how much
                        // control the weight has over the final vertices position. All weight's bias effecting a single vertex's
                        // position must add up to 1.
                    }

                    tempVert.gpuVertex.pos *= m_vertexMagnitudeMultiplier;
                }

                //*** Calculate vertex normals using normal averaging ***///
                std::vector<glm::vec3> tempNormal;

                glm::vec3 unnormalized{0.0f, 0.0f, 0.0f};

                // Compute face normals
                for (int i = 0; i < subset.numTriangles; ++i) {
                    // Get the vector describing one edge of our triangle (edge 2,0)
                    glm::vec3 edge1 = subset.vertices[subset.indices[(i * 3) + 2]].gpuVertex.pos -
                                      subset.vertices[subset.indices[(i * 3)]].gpuVertex.pos;  // Create our first edge

                    // Get the vector describing another edge of our triangle (edge 1,0)
                    glm::vec3 edge2 = subset.vertices[subset.indices[(i * 3) + 1]].gpuVertex.pos -
                                      subset.vertices[subset.indices[(i * 3)]].gpuVertex.pos;  // Create our second edge

                    // Cross multiply the two edge vectors to get the un-normalized face normal
                    unnormalized = glm::cross(edge1, edge2);

                    tempNormal.push_back(unnormalized);
                }

                // Compute vertex normals (normal Averaging)
                glm::vec3 normalSum{0.0f, 0.0f, 0.0f};

                // Go through each vertex
                for (int i = 0; i < subset.vertices.size(); ++i) {
                    // Check which triangles use this vertex
                    for (int j = 0; j < subset.numTriangles; ++j) {
                        if (subset.indices[j * 3] == i || subset.indices[(j * 3) + 1] == i || subset.indices[(j * 3) + 2] == i) {
                            // If a face is using the vertex, add the unormalized face normal to the normalSum
                            normalSum = normalSum + tempNormal[j];
                        }
                    }

                    // Normalize the normalSum vector
                    normalSum = glm::normalize(normalSum);

                    // Store the normal in our current vertex
                    subset.vertices[i].gpuVertex.normal = normalSum;

                    // Create the joint space normal for easy normal calculations in animation
                    const MD5Vertex& tempVert = subset.vertices[i];  // Get the current vertex
                    glm::vec3 normal{0.0f, 0.0f, 0.0f};

                    for (int k = 0; k < tempVert.weightCount; k++)  // Loop through each of the vertices weights
                    {
                        // Get the joints orientation
                        Joint tempJoint = m_MD5Model.joints[subset.weights[tempVert.startWeight + k].jointID];

                        // Calculate normal based off joints orientation (turn into joint space)
                        normal = tempJoint.orientation * normalSum;

                        // Store the normalized quaternion into our weights normal
                        subset.weights[tempVert.startWeight + k].normal = glm::normalize(normal);
                    }
                    // Clear normalSum, facesUsing for next vertex
                    normalSum = glm::vec3(0.0f, 0.0f, 0.0f);
                }
            }
        }
    } else {
        Utils::printLog(ERROR_PARAM, "Couldn't load animation file", absPath);
        return false;
    }

    /// packing subsets verts & indices into general containers
    std::size_t commonVertsAmount = 0u;
    std::size_t commonIndicesAmount = 0u;
    for (const auto& subset : m_MD5Model.subsets) {
        commonVertsAmount += subset.gpuVertices.size();
        commonIndicesAmount += subset.indices.size();
    }

    vertices.resize(commonVertsAmount);
    indices.resize(commonIndicesAmount);

    uint32_t lastVertsSize = 0u;
    uint32_t lastIndicesSize = 0u;
    for (auto& subset : m_MD5Model.subsets) {
        static const std::size_t indexBytes = sizeof(subset.indices[0]);
        static const std::size_t vertBytes = sizeof(subset.gpuVertices[0]);
        const std::size_t verticesSize = subset.gpuVertices.size();
        const std::size_t indicesSize = subset.indices.size();

        // eventually we'll have one single buffer containing: mesh1.indexBuf + meshN.indexBuf + ... + mesh1.vertBuf +
        // meshN.vertBuf we need to keep the offset to understand which part of buffer to update
        subset.indexOffset = lastIndicesSize;
        subset.vertOffset = lastVertsSize;

        memcpy((char*)indices.data() + lastIndicesSize * indexBytes, subset.indices.data(), indicesSize * indexBytes);
        memcpy((char*)vertices.data() + lastVertsSize * vertBytes, subset.gpuVertices.data(), verticesSize * vertBytes);

        lastVertsSize += verticesSize;
        lastIndicesSize += indicesSize;
    }

    return true;
}

void MD5Model::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    if (m_pipelineCreatorTextured->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, m_pipelineCreatorTextured->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);
    VkBuffer vertexBuffers[] = {m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);

    for (const auto& subset : m_MD5Model.subsets) {
        vkCmdBindDescriptorSets(
            cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
            m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, subset.realMaterialId), 1, &dynamicOffset);
        vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subset.indices.size()), 1, subset.indexOffset, subset.vertOffset, 0);
    }
}

void MD5Model::drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                      uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(pipelineCreator);
    assert(pipelineCreator->getPipeline().get());

    if (pipelineCreator->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, pipelineCreator->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline().get()->pipeline);

    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);
    VkBuffer vertexBuffers[] = {m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);

    for (const auto& subset : m_MD5Model.subsets) {
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline().get()->pipelineLayout, 0,
                                1, pipelineCreator->getDescriptorSet(descriptorSetIndex, subset.realMaterialId), 1,
                                &dynamicOffset);
        vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subset.indices.size()), 1, subset.indexOffset, subset.vertOffset, 0);
    }
}

void MD5Model::drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    // TODO
}
