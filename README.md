# FbxLoader

This is a basic abstraction layer for the autodesk fbxsdk. It is capable of loading models and animations.
It was originally based on https://github.com/Larry955/FbxParser but there is very little remaining from that.


To use it should only be needed to copy the h and cpp file into your project and then do:
```c++
FbxLoader::Parser parser(path);
parser.LoadScene();
```
Where you want to load the model. After that you can access the loaded meshes, animations and skeleton as member variables of the parser.
