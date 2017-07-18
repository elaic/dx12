#if !defined(IMAGE_H)
#define IMAGE_H

#include <d3d12.h>
#include <vector>

int LoadImageDataFromFile(std::vector<BYTE>& imageData, D3D12_RESOURCE_DESC& resourceDescription, LPCWSTR filename, int &bytesPerRow);

#endif // IMAGE_H
