#include "FbxLoader.h"
#include <unordered_map>

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
		std::vector<FbxNode*> _meshes;
		FindMeshes(pScene->GetRootNode(), _meshes);
		
		for (FbxNode* node : _meshes)
		{
			FbxMesh* mesh = node->GetMesh();

			
			if (mesh->GetElementBinormalCount() == 0 || mesh->GetElementTangentCount() == 0)
				mesh->GenerateTangentsDataForAllUVSets();
		}

		// Convert axis system
		FbxAxisSystem sceneAxisSystem = pScene->GetGlobalSettings().GetAxisSystem();
		FbxAxisSystem localAxisSystem(FbxAxisSystem::eDirectX);
		//if (sceneAxisSystem != localAxisSystem)
		{
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

		
		for (int i = 0; i < pScene->GetMaterialCount(); i++)
		{
			materialNameToIndexMap[pScene->GetMaterial(i)->GetName()] = i;
		}

		LoadSkeleton();
		LoadMeshes();
		LoadAnimations();

		//skeleton.Print();
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

FbxVector4 ReadNormal(FbxMesh* mesh, int vertexIndex, int vertexCounter)
{
	if (mesh->GetElementNormalCount() < 1) { return FbxVector4(0, 0, 0, 0); }

	FbxGeometryElementNormal* element = mesh->GetElementNormal(0);

	FbxVector4 normal = {};
	switch (element->GetMappingMode())
	{
	case FbxGeometryElement::eByControlPoint:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			normal = element->GetDirectArray().GetAt(vertexIndex);
		}
		break;

		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexIndex);
			normal = element->GetDirectArray().GetAt(index);
		}
		break;
		default:
			throw std::exception("Invalid Reference");
		}
		break;
	case FbxGeometryElement::eByPolygonVertex:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			normal = element->GetDirectArray().GetAt(vertexCounter);
		}
		break;
		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexCounter);
			normal = element->GetDirectArray().GetAt(index);
		}
		break;
		default: throw std::exception("Invalid Reference");
		}

		break;
	}

	return normal;
}
FbxVector4 ReadBinormal(FbxMesh* mesh, int vertexIndex, int vertexCounter)
{
	if (mesh->GetElementBinormalCount() < 1) { return FbxVector4(0, 0, 0, 0); }

	FbxGeometryElementBinormal* element = mesh->GetElementBinormal(0);

	FbxVector4 binormal = {};
	switch (element->GetMappingMode())
	{
	case FbxGeometryElement::eByControlPoint:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			binormal = element->GetDirectArray().GetAt(vertexIndex);
		}
		break;

		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexIndex);
			binormal = element->GetDirectArray().GetAt(index);
		}
		break;
		default:
			throw std::exception("Invalid Reference");
		}
		break;
	case FbxGeometryElement::eByPolygonVertex:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			binormal = element->GetDirectArray().GetAt(vertexCounter);
		}
		break;
		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexCounter);
			binormal = element->GetDirectArray().GetAt(index);
		}
		break;
		default: throw std::exception("Invalid Reference");
		}

		break;
	}

	return binormal;
}
FbxVector4 ReadTangent(FbxMesh* mesh, int vertexIndex, int vertexCounter)
{
	if (mesh->GetElementTangentCount() < 1) { return FbxVector4(0, 0, 0, 0); }

	FbxGeometryElementTangent* element = mesh->GetElementTangent(0);

	FbxVector4 tangent = {};
	switch (element->GetMappingMode())
	{
	case FbxGeometryElement::eByControlPoint:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			tangent = element->GetDirectArray().GetAt(vertexIndex);
		}
		break;

		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexIndex);
			tangent = element->GetDirectArray().GetAt(index);
		}
		break;
		default:
			throw std::exception("Invalid Reference");
		}
		break;
	case FbxGeometryElement::eByPolygonVertex:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			tangent = element->GetDirectArray().GetAt(vertexCounter);
		}
		break;
		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexCounter);
			tangent = element->GetDirectArray().GetAt(index);
		}
		break;
		default: throw std::exception("Invalid Reference");
		}
		break;
	}

	return tangent;
}
FbxVector2 ReadUV(FbxMesh* mesh, int vertexIndex, int vertexCounter)
{
	if (mesh->GetElementUVCount() < 1) { return FbxVector2(0, 0); }

	FbxGeometryElementUV* element = mesh->GetElementUV(0);

	FbxVector2 uv = {};
	switch (element->GetMappingMode())
	{
	case FbxGeometryElement::eByControlPoint:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			uv = element->GetDirectArray().GetAt(vertexIndex);
		}
		break;

		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexIndex);
			uv = element->GetDirectArray().GetAt(index);
		}
		break;
		default:
			throw std::exception("Invalid Reference");
		}
		break;
	case FbxGeometryElement::eByPolygonVertex:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			uv = element->GetDirectArray().GetAt(vertexCounter);
		}
		break;
		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexCounter);
			uv = element->GetDirectArray().GetAt(index);
		}
		break;
		default: throw std::exception("Invalid Reference");
		}
		break;
	}

#if FLIP_UV_Y
	uv.mData[1] = 1.0f - uv.mData[1];
#endif

	return uv;
}
FbxColor ReadVertexColor(FbxMesh* mesh, int vertexIndex, int vertexCounter)
{
	if (mesh->GetElementVertexColorCount() < 1) { return FbxColor(1, 1, 1, 1); }

	FbxGeometryElementVertexColor* element = mesh->GetElementVertexColor(0);

	FbxColor color = {};
	switch (element->GetMappingMode())
	{
	case FbxGeometryElement::eByControlPoint:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			color = element->GetDirectArray().GetAt(vertexIndex);
		}
		break;

		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexIndex);
			color = element->GetDirectArray().GetAt(index);
		}
		break;
		default:
			throw std::exception("Invalid Reference");
		}
		break;
	case FbxGeometryElement::eByPolygonVertex:
		switch (element->GetReferenceMode())
		{
		case FbxGeometryElement::eDirect:
		{
			color = element->GetDirectArray().GetAt(vertexCounter);
		}
		break;
		case FbxGeometryElement::eIndexToDirect:
		{
			int index = element->GetIndexArray().GetAt(vertexCounter);
			color = element->GetDirectArray().GetAt(index);
		}
		break;
		default: throw std::exception("Invalid Reference");
		}
		break;
	}

	return color;
}
int ReadMaterial(FbxMesh* mesh, int polygon)
{
	if (mesh->GetElementMaterialCount() < 1) { return 0; }

	//FbxGeometryElementMaterial* element = mesh->GetElementMaterial(0);

	FbxLayerElementArrayTemplate<int>* materialArray;
	mesh->GetMaterialIndices(&materialArray);

	int material = materialArray->GetAt(polygon);

	return material;
}

FbxVector4 TransformFbxVector4(const FbxAMatrix& matrix, const FbxVector4& vector)
{
	// Extract the matrix components
	FbxVector4 translation = matrix.GetT();
	FbxVector4 rotation = matrix.GetR();
	FbxVector4 scaling = matrix.GetS();

	// Convert rotation to radians as FbxVector4 assumes degrees
	//double pi = 3.14159265358979323846;
	//FbxVector4 rotationRadians(rotation[0] * pi / 180.0, rotation[1] * pi / 180.0, rotation[2] * pi / 180.0);

	// Create rotation matrices
	FbxAMatrix rotationMatrix;
	rotationMatrix.SetR(rotation);

	// Create scaling matrix
	FbxAMatrix scalingMatrix;
	scalingMatrix.SetS(scaling);

	// Apply rotation and scaling
	FbxVector4 transformedVector = scalingMatrix.MultT(rotationMatrix.MultT(vector));

	// Apply translation
	transformedVector += translation;

	return transformedVector;
}

void FbxLoader::Parser::LoadMesh(FbxNode* node)
{
	FbxMesh *mesh = node->GetMesh();
	if (!mesh) {
		return;
	}

	FbxAMatrix transform = node->EvaluateGlobalTransform();

	FbxLoader::Mesh result = {};

	int polyCount = mesh->GetPolygonCount();
	int deformerCount = mesh->GetDeformerCount();

	std::unordered_map<size_t, size_t> hashToRealIndex;
	std::unordered_map<size_t, std::vector<size_t>> controlPointIndexToRealIndex;

	int vertexCounter = 0;
	for (int polygon = 0; polygon < polyCount; polygon++)
	{
		for (int polygonVertex = 0; polygonVertex < 3; polygonVertex++)
		{
			int controlPointIndex = mesh->GetPolygonVertex(polygon, polygonVertex);
			
			Mesh::VertexData data = {};
			data.position = TransformFbxVector4(transform, mesh->GetControlPointAt(controlPointIndex)) * scaleFactor;
			data.normal = ReadNormal(mesh, controlPointIndex, vertexCounter);
			data.binormal = ReadBinormal(mesh, controlPointIndex, vertexCounter);
			data.tangent = ReadTangent(mesh, controlPointIndex, vertexCounter);
			data.uv = ReadUV(mesh, controlPointIndex, vertexCounter);
			data.color = ReadVertexColor(mesh, controlPointIndex, vertexCounter);
			data.materialIndex = ReadMaterial(mesh, vertexCounter);
			FbxSurfaceMaterial* material = mesh->GetNode()->GetMaterial(data.materialIndex);
			if (material)
				data.materialIndex = materialNameToIndexMap[material->GetName()];

			size_t hash = Mesh::hash_vert(data);
			if (hashToRealIndex.count(hash) > 0)
			{
				// Has found earlier
				result.indices.push_back(hashToRealIndex[hash]);
				controlPointIndexToRealIndex[controlPointIndex].push_back(hashToRealIndex[hash]);
			}
			else
			{
				// New vertex
				hashToRealIndex[hash] = result.vertices.size();
				controlPointIndexToRealIndex[controlPointIndex].push_back(result.vertices.size());
				result.indices.push_back(result.vertices.size());
				result.vertices.push_back(data);
			}

			vertexCounter++;
		}
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
				int controlPointIndex = currCluster->GetControlPointIndices()[i];

				if (weight < 0.01f)
					continue;

				for (int vertexIndex = 0; vertexIndex < controlPointIndexToRealIndex[controlPointIndex].size(); vertexIndex++)
				{
					Mesh::VertexData& vertex = result.vertices[controlPointIndexToRealIndex[controlPointIndex][vertexIndex]];

					if (vertex.jointCount >= (MAX_VERTEX_BONES - 1))
						continue;

					bool found = false;
					for (int vertexBoneIndex = 0; vertexBoneIndex < vertex.jointCount; vertexBoneIndex++)
					{
						if ((int)vertex.jointIndices[vertexBoneIndex] == currJointIndex)
						{
							found = true;
							break;
						}
					}
					if (found)
						continue;

					vertex.jointWeights[vertex.jointCount] = weight;
					vertex.jointIndices[vertex.jointCount] = currJointIndex;
					vertex.jointCount++;
				}
			}
		}
	}

	meshes.push_back(result);
}
void FbxLoader::Parser::LoadMeshes()
{
	std::vector<FbxNode*> _meshes;
	FindMeshes(pScene->GetRootNode(), _meshes);
	for (auto mesh : _meshes) 
	{
		LoadMesh(mesh);
	}

	materialCount = pScene->GetSrcObjectCount<FbxSurfaceMaterial>();

#if SPLIT_MESH_MATERIAL
	std::vector<Mesh> _optimized_meshes;
	for (int materialIndex = 0; materialIndex < materialCount; materialIndex++)
	{
		FbxSurfaceMaterial* material = pScene->GetSrcObject<FbxSurfaceMaterial>(materialIndex);
		FbxString materialName = material->GetName();
		Mesh optimizedMesh = {};
		optimizedMesh.materialName = materialName;
		optimizedMesh.materialIndex = materialIndex;

		for (Mesh& mesh : meshes)
		{
			std::unordered_map<size_t, size_t> hashToRealIndex;

			for (int oldIndex = 0; oldIndex < mesh.indices.size(); oldIndex++)
			{
				if (mesh.vertices[mesh.indices[oldIndex]].materialIndex != materialIndex)
					continue;

				size_t hash = Mesh::hash_vert(mesh.vertices[mesh.indices[oldIndex]]);
				if (hashToRealIndex.count(hash) == 0)
				{
					hashToRealIndex[hash] = optimizedMesh.vertices.size();
					optimizedMesh.indices.push_back(optimizedMesh.vertices.size());
					optimizedMesh.vertices.push_back(mesh.vertices[mesh.indices[oldIndex]]);
				}
				else
				{
					optimizedMesh.indices.push_back(hashToRealIndex[hash]);
				}
			}
		}

		if (optimizedMesh.vertices.size() > 0)
		{
			_optimized_meshes.push_back(optimizedMesh);
		}
	}

	if (materialCount > 0)
	{
		meshes.clear();
		meshes = _optimized_meshes;
	}
#endif
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

		std::string name = jointTmp.jointName.Buffer();
		skeleton.jointMap[name] = (int)skeleton.joints.size();

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
		LoadSkeleton(rootNode->GetChild(i), 0, (int)skeleton.joints.size(), -1);
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
	for (FbxLongLong frameIndex = 0; frameIndex < result.frameCount; frameIndex++)
	{
		FbxTime currTime;
		currTime.SetFrame(frameIndex, timeMode);

		result.globalTransforms[boneIndex][frameIndex] = GetGlobalTransform(node, currTime);
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

		if (animStackCount > 1)
			break;
	}
}

FbxAMatrix FbxLoader::Parser::GetGlobalTransform(FbxNode* node, FbxTime time /*= FBXSDK_TIME_INFINITE*/)
{
	FbxAMatrix globalTransform = node->EvaluateGlobalTransform(time);
	globalTransform.SetT(globalTransform.GetT() * scaleFactor/** 0.01f*/);
	return globalTransform;
}

FbxAMatrix FbxLoader::Parser::GetLocalTransform(FbxNode* node, FbxTime time /*= FBXSDK_TIME_INFINITE*/)
{
	FbxAMatrix localTransform = node->EvaluateLocalTransform(time);
	localTransform.SetT(localTransform.GetT() * scaleFactor/** 0.01f*/);
	return localTransform;
}
