#ifndef FBXPARSER_H
#define FBXPARSER_H

#include <vector>
#include "fbxsdk.h"

// Configuration
#define MAX_VERTEX_BONES 4
#define FLIP_UV_Y 1
#define SPLIT_MESH_MATERIAL 1

namespace FbxLoader
{
	struct Joint 
	{
		fbxsdk::FbxString jointName;
		int currentIndex;	//index of current joint	
		int parentIndex;	//index to its parent joint
		fbxsdk::FbxAMatrix globalMatrix;
		fbxsdk::FbxAMatrix localMatrix;
		fbxsdk::FbxNode* node;

		Joint() :
			node(nullptr)
		{
			localMatrix.SetIdentity();
			globalMatrix.SetIdentity();
			parentIndex = -1;
		}
	};

	struct Skeleton
	{
		std::vector<Joint> joints;
		std::unordered_map<std::string, int> jointMap;
		void Print()
		{
			for (int i = 0; i < joints.size(); i++)
			{
				printf("Joint %d: %s\n", i, joints[i].jointName.Buffer());
				/*printf("Parent: %d\n", joints[i].parentIndex);
				printf("Local Matrix:\n");
				for (int row = 0; row < 4; row++)
				{
					for (int col = 0; col < 4; col++)
					{
						printf("%f ", joints[i].localMatrix.Get(row, col));
					}
					printf("\n");
				}
				printf("Global Matrix:\n");
				for (int row = 0; row < 4; row++)
				{
					for (int col = 0; col < 4; col++)
					{
						printf("%f ", joints[i].globalMatrix.Get(row, col));
					}
					printf("\n");
				}*/
			}
		}
	};

	struct Mesh
	{
		struct VertexData
		{
			FbxVector4 position = {};
			FbxVector4 normal = {};
			FbxVector4 binormal = {};
			FbxVector4 tangent = {};
			FbxVector2 uv = {};
			FbxColor color = {};
			int materialIndex = 0;

			int jointCount = {};
			unsigned int jointIndices[MAX_VERTEX_BONES] = {};
			float jointWeights[MAX_VERTEX_BONES] = {};
		};
		static inline size_t hash_vert(VertexData& data) // This is one ghetto ass hashing function. Should probably use something already available.
		{
			char* byte_array = (char*)&data;
			size_t hash = 0;

			for (int i = 0; i < sizeof(data); i++)
				hash = hash * 31 + byte_array[i];

			return hash;
		}

		std::vector<size_t> indices;
		std::vector<VertexData> vertices;

		fbxsdk::FbxAMatrix meshToWorld;

		fbxsdk::FbxString materialName;
		int materialIndex;
	};

	struct Animation
	{
		fbxsdk::FbxString name;
		fbxsdk::FbxDouble length;
		fbxsdk::FbxDouble frameRate;
		fbxsdk::FbxLongLong frameCount;
		
		fbxsdk::FbxAMatrix** localTransforms;	// [boneIndex][frameIndex]
		fbxsdk::FbxAMatrix** globalTransforms;	// [boneIndex][frameIndex]

		fbxsdk::FbxAMatrix CalcGlobalTransform(int boneIndex, fbxsdk::FbxLongLong frameIndex, Skeleton* skeleton) // Manual way of calculating global transform for a bone.
		{
			if (boneIndex == -1)
			{
				fbxsdk::FbxAMatrix identity;
				identity.SetIdentity();
				return identity;
			}

			fbxsdk::FbxAMatrix local = localTransforms[boneIndex][frameIndex];
			fbxsdk::FbxAMatrix parentGlobal = CalcGlobalTransform(skeleton->joints[boneIndex].parentIndex, frameIndex, skeleton);
			globalTransforms[boneIndex][frameIndex] = parentGlobal * local;

			return globalTransforms[boneIndex][frameIndex];
		}
	};

	class Parser
	{
	public:
		Parser(FbxString fbxFile);
		~Parser();

		Parser(const Parser&) = delete;
		Parser& operator=(const Parser&) = delete;

		bool LoadScene(); // Load scene, return false if failed

		Skeleton skeleton;
		std::vector<FbxLoader::Mesh> meshes;
		std::vector<FbxLoader::Animation> animations;

		float scaleFactor = 1.0f;

		int materialCount = 0;
	private:
		fbxsdk::FbxManager* pManager = nullptr;
		fbxsdk::FbxIOSettings* ios = nullptr;
		fbxsdk::FbxScene* pScene;
		fbxsdk::FbxString fbxFile;
		std::unordered_map<std::string, int> materialNameToIndexMap;

		void InitFbxObjects();

		int Parser::FindJointIndexByName(const FbxString& jointName)
		{
			for (int index = 0; index != skeleton.joints.size(); ++index) {
				if (skeleton.joints[index].jointName == jointName)
					return index;
			}
			return -1;
		}

		FbxNode* FindMesh(FbxNode* node);
		void FindMeshes(FbxNode* node, std::vector<FbxNode*>& meshes);

		void LoadMesh(FbxNode* node);
		void LoadMeshes();

		void LoadSkeleton(FbxNode* node, int depth, int currIndex, int parentIndex);
		void LoadSkeleton();

		void LoadAnimation(Joint* joint, FbxLoader::Animation& result);
		FbxLoader::Animation Parser::LoadAnimation(FbxAnimStack* animStack);
		void LoadAnimations();

		// Internal helper functions for getting transform matrices with correct scale on translation
		fbxsdk::FbxAMatrix GetGlobalTransform(fbxsdk::FbxNode* node, fbxsdk::FbxTime time = FBXSDK_TIME_INFINITE);
		fbxsdk::FbxAMatrix GetLocalTransform(fbxsdk::FbxNode* node, fbxsdk::FbxTime time = FBXSDK_TIME_INFINITE);
	};
}
 
#endif 