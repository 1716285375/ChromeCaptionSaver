#pragma once
#include <vector>
#include <wincodec.h>
// Save only this application's rendered backbuffer; unrelated desktop windows
// are never captured.
inline bool SaveRenderSnapshot(IDXGISwapChain *chain, ID3D11Device *device,
                               ID3D11DeviceContext *context,
                               const wchar_t *path) {
  using Microsoft::WRL::ComPtr;
  ComPtr<ID3D11Texture2D> source, staging;
  if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&source))))
    return false;
  D3D11_TEXTURE2D_DESC desc{};
  source->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging)))
    return false;
  context->CopyResource(staging.Get(), source.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return false;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr))
    hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr))
    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
  if (SUCCEEDED(hr))
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (SUCCEEDED(hr))
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (SUCCEEDED(hr))
    hr = encoder->CreateNewFrame(&frame, nullptr);
  if (SUCCEEDED(hr))
    hr = frame->Initialize(nullptr);
  if (SUCCEEDED(hr))
    hr = frame->SetSize(desc.Width, desc.Height);
  std::vector<BYTE> pixels(desc.Width * desc.Height * 4);
  for (UINT y = 0; y < desc.Height; ++y)
    for (UINT x = 0; x < desc.Width; ++x) {
      auto *src =
          static_cast<BYTE *>(mapped.pData) + y * mapped.RowPitch + x * 4;
      auto *dst = pixels.data() + (y * desc.Width + x) * 4;
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
      dst[3] = src[3];
    }
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr))
    hr = frame->SetPixelFormat(&format);
  if (SUCCEEDED(hr) && format != GUID_WICPixelFormat32bppBGRA)
    hr = E_FAIL;
  if (SUCCEEDED(hr))
    hr = frame->WritePixels(desc.Height, desc.Width * 4,
                            static_cast<UINT>(pixels.size()), pixels.data());
  if (SUCCEEDED(hr))
    hr = frame->Commit();
  if (SUCCEEDED(hr))
    hr = encoder->Commit();
  context->Unmap(staging.Get(), 0);
  return SUCCEEDED(hr);
}
