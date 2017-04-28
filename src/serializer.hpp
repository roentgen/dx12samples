/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#ifndef ROOT_SIGNATURE_SERIALIZER_HPP__
#define ROOT_SIGNATURE_SERIALIZER_HPP__

#include <D3D12.h>

HRESULT serialize_root_signature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob** blob, ID3DBlob** err);

#endif
