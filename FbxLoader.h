#ifndef FBXPARSER_H
#define FBXPARSER_H

#include <vector>
#include <unordered_map>
#include "fbxsdk.h"

namespace FbxLoader
{
	struct Joint {
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

	struct Skeleton {
		std::vector<Joint> joints;
	};

	struct Mesh
	{
		struct JointIndices
		{
			int indices[4];
		};
		struct JointWeights
		{
			float weights[4];
		};

		// Index data
		std::vector<int> indices;

		// Vertex data
		std::vector<fbxsdk::FbxVector4> positions;
		std::vector<fbxsdk::FbxVector4> normals;
		std::vector<fbxsdk::FbxVector4> tangents;
		std::vector<fbxsdk::FbxVector4> bitangents;
		std::vector<fbxsdk::FbxVector2> uvs;
		std::vector<fbxsdk::FbxVector4> colors;
		std::vector<JointIndices> jointIndices;
		std::vector<JointWeights> jointWeights;
		std::vector<int> jointCount;
		fbxsdk::FbxAMatrix meshToWorld;

		// Material data
		fbxsdk::FbxString materialName;
	};

	struct Animation
	{
		fbxsdk::FbxString name;
		fbxsdk::FbxDouble length;
		fbxsdk::FbxDouble frameRate;
		fbxsdk::FbxLongLong frameCount;
		
		fbxsdk::FbxAMatrix** localTransforms; // [boneIndex][frameIndex]
		fbxsdk::FbxAMatrix** globalTransforms;// [boneIndex][frameIndex]
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
	private:
		fbxsdk::FbxManager* pManager = nullptr;
		fbxsdk::FbxIOSettings* ios = nullptr;
		fbxsdk::FbxScene* pScene;
		fbxsdk::FbxString fbxFile;

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
	};

	// Internal helper functions for getting transform matrices with correct scale on translation
	fbxsdk::FbxAMatrix GetGlobalTransform(fbxsdk::FbxNode* node, fbxsdk::FbxTime time = FBXSDK_TIME_INFINITE);
	fbxsdk::FbxAMatrix GetLocalTransform(fbxsdk::FbxNode* node, fbxsdk::FbxTime time = FBXSDK_TIME_INFINITE);
}
 
#endif 