#include "FbxLoader.h"
#include <assert.h>
#include <math.h>

FbxLoader::Parser::Parser(FbxString fbxFile)
{
	this->fbxFile = fbxFile;
	InitFbxObjects();
}

FbxLoader::Parser::~Parser()
{
	pManager->Destroy();
}

void FbxLoader::Parser::InitFbxObjects()
{
	// Create the FBX manager which is the object allocator for almost all the classes in the SDK
	if (!pManager)
		pManager = FbxManager::Create();

	if (!pManager)
	{
		FBXSDK_printf("error: unable to create FBX manager!\n");
		exit(1);
	}
	else
	{
		FBXSDK_printf("Autodesk FBX SDK version:%s\n", pManager->GetVersion());
	}

	// Create an IOSettings object. This object holds all import/export settings
	ios = FbxIOSettings::Create(pManager, IOSROOT);
	pManager->SetIOSettings(ios);

	/* // NOTE(Eric): THIS IS FUCKED! This causes the manager to take forever during deletion.
	//load plugins from the executable directory
	FbxString path = FbxGetApplicationDirectory();
	//path += "/fbx_plugins/";
	pManager->LoadPluginsDirectory(path.Buffer());
	*/

	// Create an FBX scene. This object holds most objects imported/exported from/to files
	pScene = FbxScene::Create(pManager, "my scene");
	if (!pScene) 
	{
		FBXSDK_printf("error: unable to create FBX scene\n");
		exit(1);
	}
}

bool FbxLoader::Parser::LoadScene()
{
	FbxString fullFbxFile;
	if (fbxFile.Find(".fbx") == -1)
	{
		fullFbxFile = fbxFile + ".fbx";
	}
	else 
	{
		fullFbxFile = fbxFile;
	}
	if (!FbxFileUtils::Exist(fullFbxFile))
	{
		return false;
	}
	assert(pManager != nullptr && pScene != nullptr);

	bool status = false;
	FbxImporter *importer = FbxImporter::Create(pManager, "");

	const bool imorterStatus = importer->Initialize(fullFbxFile, -1, pManager->GetIOSettings());
	if (!imorterStatus) 
	{
		FBXSDK_printf("error: initialize importer failed\n");
		return status;
	}

	if (!importer->IsFBX())
	{
		FBXSDK_printf("error: file is not a FBX file\n");
		return status;
	}

	status = importer->Import(pScene);

	if (status)
	{
		// Convert axis system
		FbxAxisSystem sceneAxisSystem = pScene->GetGlobalSettings().GetAxisSystem();
		FbxAxisSystem localAxisSystem(FbxAxisSystem::MayaYUp);
		//FbxAxisSystem localAxisSystem(FbxAxisSystem::eDirectX);
		if (sceneAxisSystem != localAxisSystem)
		{
			//localAxisSystem.ConvertScene(pScene);
			localAxisSystem.DeepConvertScene(pScene);
		}

		// Convert unit system
		FbxSystemUnit sceneSystemUnit = pScene->GetGlobalSettings().GetSystemUnit();
		/*
		if (sceneSystemUnit.GetScaleFactor() != FbxSystemUnit::m.GetScaleFactor())
		{
			FbxSystemUnit::m.ConvertScene(pScene); // NOTE(Eric): This does not actually seem to work so we do it manually.
		}
		*/
		scaleFactor = (float)FbxSystemUnit::m.GetConversionFactorFrom(sceneSystemUnit);

		// Convert mesh, NURBS and patch into triangle mesh
		FbxGeometryConverter geomConverter(pManager);
		if (!geomConverter.Triangulate(pScene, true, true)) // Attempt to use faster legacy triangulation algorithm
			geomConverter.Triangulate(pScene, true, false);

		LoadSkeleton();
		LoadMeshes();
		LoadAnimations();
	}

	pScene->Destroy();
	importer->Destroy();

	return status;
}

FbxNode* FbxLoader::Parser::FindMesh(FbxNode* _node)
{
	if (_node->GetNodeAttribute() && _node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh) {
		return _node;
	}
	for (int i = 0; i != _node->GetChildCount(); ++i) {
		FbxNode* node = FindMesh(_node->GetChild(i));
		if (node) {
			return node;
		}
	}
	return nullptr;
}
void FbxLoader::Parser::FindMeshes(FbxNode* node, std::vector<FbxNode*>& _meshes)
{
	if (node->GetNodeAttribute() && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh) {
		_meshes.push_back(node);
	}
	for (int i = 0; i != node->GetChildCount(); ++i) {
		FindMeshes(node->GetChild(i), _meshes);
	}
}

template<typename Element, typename ElementType = FbxVector4>
void GetGeometryElement(FbxMesh* mesh, std::vector<ElementType>& elements)
{
	int elementCount = 0;
	Element* element = nullptr;

	if constexpr (std::is_same<Element, FbxGeometryElementNormal>()) { elementCount = mesh->GetElementNormalCount(); element = mesh->GetElementNormal(0); }
	else if constexpr (std::is_same<Element, FbxGeometryElementBinormal>()) { elementCount = mesh->GetElementBinormalCount(); element = mesh->GetElementBinormal(0); }
	else if constexpr (std::is_same<Element, FbxGeometryElementTangent>()) { elementCount = mesh->GetElementTangentCount(); element = mesh->GetElementTangent(0); }
	else if constexpr (std::is_same<Element, FbxGeometryElementUV>()) { elementCount = mesh->GetElementUVCount(); element = mesh->GetElementUV(0); }
	else if constexpr (std::is_same<Element, FbxGeometryElementVertexColor>()) { elementCount = mesh->GetElementVertexColorCount(); element = mesh->GetElementVertexColor(0); }
	else { static_assert(false, "Geometry element type not registered!"); }

	if (elementCount < 1) return;

	elements.resize(mesh->GetControlPointsCount());
	memset(elements.data(), 0, sizeof(ElementType) * elements.size());

	// Loop through each polygon and each vertex in the polygon
	int polygonCount = mesh->GetPolygonCount();
	int vertexCounter = 0;
	for (int poly = 0; poly < polygonCount; poly++)
	{
		for (int polyVertIndex = 0; polyVertIndex < 3; polyVertIndex++)
		{
			int vertexIndex = mesh->GetPolygonVertex(poly, polyVertIndex);

			ElementType result = {};
			switch (element->GetMappingMode())
			{
			case FbxGeometryElement::eByControlPoint:
				switch (element->GetReferenceMode())
				{
				case FbxGeometryElement::eDirect:
				{
					auto temp = element->GetDirectArray().GetAt(vertexIndex);
					result = *(ElementType*)&temp;
				} break;
				case FbxGeometryElement::eIndexToDirect:
				{
					int index = element->GetIndexArray().GetAt(vertexIndex);
					auto temp = element->GetDirectArray().GetAt(index);
					result = *(ElementType*)&temp;
				} break;
				}
				break;
			case FbxGeometryElement::eByPolygonVertex:
				switch (element->GetReferenceMode())
				{
				case FbxGeometryElement::eDirect:
				{
					auto temp = element->GetDirectArray().GetAt(vertexCounter);
					result = *(ElementType*)&temp;
				} break;
				case FbxGeometryElement::eIndexToDirect:
				{
					int index = element->GetIndexArray().GetAt(vertexCounter);
					auto temp = element->GetDirectArray().GetAt(index);
					result = *(ElementType*)&temp;
				} break;
				default:
					throw std::exception("Invalid Reference");
				}
			default:
				break;
			}

			elements[vertexIndex] = result;
			vertexCounter++;
		}
	}
}

void FbxLoader::Parser::LoadMesh(FbxNode* node)
{
	FbxMesh *mesh = node->GetMesh();
	if (!mesh) {
		return;
	}

	FbxLoader::Mesh result = {};

	//get the number of polygons
	int _polygonCount = mesh->GetPolygonCount();
	for (int i = 0; i < _polygonCount; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			int vertexIndex = mesh->GetPolygonVertex(i, j);
			result.indices.push_back(vertexIndex);
		}
	}

	//get the number of vertices
	int vertexCount = mesh->GetControlPointsCount(); vertexCount;
	for (int i = 0; i < mesh->GetControlPointsCount(); i++)
	{
		/*if (skeleton.joints.size() > 0)
		{
			result.positions.push_back(mesh->GetControlPointAt(i));
		}
		else*/
		{
			result.positions.push_back(mesh->GetControlPointAt(i) * scaleFactor);
		}
	}

	//get the number of materials
	int materialCount = node->GetSrcObjectCount<FbxSurfaceMaterial>(); materialCount;
	if (materialCount > 0)
		result.materialName = node->GetSrcObject<FbxSurfaceMaterial>(0)->GetName();

	GetGeometryElement<FbxGeometryElementNormal, FbxVector4>(mesh, result.normals);
	GetGeometryElement<FbxGeometryElementBinormal, FbxVector4>(mesh, result.bitangents);
	GetGeometryElement<FbxGeometryElementTangent, FbxVector4>(mesh, result.tangents);
	GetGeometryElement<FbxGeometryElementUV, FbxVector2>(mesh, result.uvs);
	GetGeometryElement<FbxGeometryElementVertexColor, FbxVector4>(mesh, result.colors);

	//get joints and weights
	int deformerCount = mesh->GetDeformerCount();
	if (deformerCount > 0)
	{
		result.jointWeights.resize(result.positions.size());
		result.jointIndices.resize(result.positions.size());
		result.jointCount.resize(result.positions.size());
	}

	for (int i = 0; i < result.jointIndices.size(); i++)
	{
		result.jointIndices[i] = { 0, 0, 0, 0 };
		result.jointWeights[i] = { 0, 0, 0, 0 };
		result.jointCount[i] = 0;
	}

	for (int deformerIndex = 0; deformerIndex < deformerCount; deformerIndex++)
	{
		FbxSkin* currSkin = (FbxSkin*)(mesh->GetDeformer(deformerIndex, FbxDeformer::eSkin));
		if (!currSkin) {
			continue;
		}

		unsigned int numOfClusters = currSkin->GetClusterCount();
		for (unsigned int clusterIndex = 0; clusterIndex != numOfClusters; ++clusterIndex)
		{
			FbxCluster* currCluster = currSkin->GetCluster(clusterIndex);
			FbxString currJointName = currCluster->GetLink()->GetName();
			int currJointIndex = FindJointIndexByName(currJointName);
			if (currJointIndex == -1)
			{
				FBXSDK_printf("error: can't find the joint: %s\n\n", currJointName.Buffer());
				continue;
			}

			int numOfIndices = currCluster->GetControlPointIndicesCount();
			for (int i = 0; i < numOfIndices; ++i)
			{
				float weight = (float)currCluster->GetControlPointWeights()[i];
				int index = currCluster->GetControlPointIndices()[i];

				if (weight < 0.01f || result.jointCount[index] >= 3)
				{
					continue;
				}

				result.jointWeights[index].weights[result.jointCount[index]] = weight;
				result.jointIndices[index].indices[result.jointCount[index]] = currJointIndex;
				result.jointCount[index]++;
			}
		}
	}

	meshes.push_back(result);
}
void FbxLoader::Parser::LoadMeshes()
{
	std::vector<FbxNode*> _meshes;
	FindMeshes(pScene->GetRootNode(), _meshes);
	for (auto mesh : _meshes) {
		LoadMesh(mesh);
	}
}

void FbxLoader::Parser::LoadSkeleton(FbxNode* node, int depth, int currIndex, int parentIndex)
{
	if (node->GetNodeAttribute() && node->GetNodeAttribute()->GetAttributeType() &&
		node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
	{
		Joint jointTmp = {};
		jointTmp.jointName = node->GetName();
		jointTmp.parentIndex = parentIndex;
		jointTmp.currentIndex = currIndex;

		jointTmp.localMatrix = GetLocalTransform(node);
		jointTmp.globalMatrix = GetGlobalTransform(node);

		jointTmp.node = node;

		skeleton.joints.push_back(jointTmp);
	}

	for (int i = 0; i != node->GetChildCount(); ++i)
	{
		LoadSkeleton(node->GetChild(i), depth + 1, (int)skeleton.joints.size(), currIndex);
	}
}
void FbxLoader::Parser::LoadSkeleton()
{
	FbxNode* rootNode = pScene->GetRootNode();
	int childCount = rootNode->GetChildCount();
	for (int i = 0; i != childCount; ++i) {
		LoadSkeleton(rootNode->GetChild(i), 0, 0, -1);
	}
}

void FbxLoader::Parser::LoadAnimation(Joint* joint, FbxLoader::Animation& result)
{
	FbxNode* node = joint->node;
	int boneIndex = joint->currentIndex;

	FbxGlobalSettings& globalSettings = pScene->GetGlobalSettings();
	FbxTime::EMode timeMode = globalSettings.GetTimeMode();

	result.globalTransforms[boneIndex] = new FbxAMatrix[result.frameCount];
	result.localTransforms[boneIndex] = new FbxAMatrix[result.frameCount];
	FbxAMatrix offsetMatrix;
	for (FbxLongLong frameIndex = 0; frameIndex < result.frameCount; frameIndex++)
	{
		FbxTime currTime;
		currTime.SetFrame(frameIndex, timeMode);

		if (frameIndex == 0)
			offsetMatrix = GetGlobalTransform(node, currTime).Inverse();

		result.globalTransforms[boneIndex][frameIndex] = /*offsetMatrix **/ GetGlobalTransform(node, currTime);
		result.localTransforms[boneIndex][frameIndex] = GetLocalTransform(node, currTime);
	}
}
FbxLoader::Animation FbxLoader::Parser::LoadAnimation(FbxAnimStack* animStack)
{
	FbxGlobalSettings& globalSettings = pScene->GetGlobalSettings();
	FbxTime::EMode timeMode = globalSettings.GetTimeMode();

	FbxLoader::Animation result = {};
	result.name = animStack->GetName();
	result.length = animStack->GetLocalTimeSpan().GetDuration().GetSecondDouble();
	result.frameRate = animStack->GetLocalTimeSpan().GetDuration().GetFrameCountPrecise(timeMode) / result.length;
	result.frameCount = animStack->GetLocalTimeSpan().GetDuration().GetFrameCount(timeMode);

	result.localTransforms = new FbxAMatrix * [skeleton.joints.size()];
	result.globalTransforms = new FbxAMatrix * [skeleton.joints.size()];

	for (int i = 0; i < skeleton.joints.size(); i++)
	{
		LoadAnimation(&skeleton.joints[i], result);
	}

	return result;
}
void FbxLoader::Parser::LoadAnimations()
{
	int animStackCount = pScene->GetSrcObjectCount<FbxAnimStack>();
	animations.resize(animStackCount);
	for (int animIndex = 0; animIndex < animStackCount; animIndex++)
	{
		FbxAnimStack* animStack = pScene->GetSrcObject<FbxAnimStack>(animIndex);
		pScene->SetCurrentAnimationStack(animStack);

		animations[animIndex] = LoadAnimation(animStack);
	}
}

FbxAMatrix FbxLoader::GetGlobalTransform(FbxNode* node, FbxTime time /*= FBXSDK_TIME_INFINITE*/)
{
	FbxAMatrix globalTransform = node->EvaluateGlobalTransform(time);
	globalTransform.SetT(globalTransform.GetT() * 0.01f);
	return globalTransform;
}

FbxAMatrix FbxLoader::GetLocalTransform(FbxNode* node, FbxTime time /*= FBXSDK_TIME_INFINITE*/)
{
	FbxAMatrix localTransform = node->EvaluateLocalTransform(time);
	localTransform.SetT(localTransform.GetT() * 0.01f);
	return localTransform;
}
