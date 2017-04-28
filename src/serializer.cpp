/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
#include "stdafx.h"
#include "serializer.hpp"
#include <stdint.h>
#include "dbgutils.hpp"

HRESULT serialize_root_signature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob** blob, ID3DBlob** err)
{
	if (err)
		*err = NULL;

	/* 1.1 までサポートされていれば何も考える必要はない */
	if (version == D3D_ROOT_SIGNATURE_VERSION_1_1) {
		INF("serialize_rootsignature: support version:0x%x\n", version);
		return D3D12SerializeVersionedRootSignature(desc, blob, err);
	}

	/* 1.0 までしかサポートしていないのに、 desc->Version が 1.1 だとマイグレーションが必要 */
	if (version == D3D_ROOT_SIGNATURE_VERSION_1_0) {
		INF("serialize_rootsignature: support version:0x%x (old)\n", version);
		switch (desc->Version) {
		case D3D_ROOT_SIGNATURE_VERSION_1_0: /* サポートバージョンと要求バージョンが一致するなら問題はない */
			INF("serialize_rootsignature: version matched to request:0x%x (old)\n", desc->Version);
			return D3D12SerializeRootSignature(&desc->Desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, blob, err);
			
		case D3D_ROOT_SIGNATURE_VERSION_1_1:
		{
			INF("serialize_rootsignature: version unmatched to request:0x%x needs migration\n", desc->Version);
			HRESULT hr = S_OK;
			const D3D12_ROOT_SIGNATURE_DESC1& desc_1_1 = desc->Desc_1_1;
			
			const size_t size = sizeof(D3D12_ROOT_PARAMETER) * desc_1_1.NumParameters;
			void* params = (size > 0) ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
			if (size > 0 && params == nullptr)
				hr = E_OUTOFMEMORY;
			D3D12_ROOT_PARAMETER* param_1_0 = reinterpret_cast<D3D12_ROOT_PARAMETER*>(params);

			if (SUCCEEDED(hr)) {
				for (UINT n = 0; n < desc_1_1.NumParameters; n++) {
					__analysis_assume(size == sizeof(D3D12_ROOT_PARAMETER) * desc_1_1.NumParameters);
					param_1_0[n].ParameterType = desc_1_1.pParameters[n].ParameterType;
					param_1_0[n].ShaderVisibility = desc_1_1.pParameters[n].ShaderVisibility;
					
					switch (desc_1_1.pParameters[n].ParameterType) {
					case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
						param_1_0[n].Constants.Num32BitValues = desc_1_1.pParameters[n].Constants.Num32BitValues;
						param_1_0[n].Constants.RegisterSpace = desc_1_1.pParameters[n].Constants.RegisterSpace;
						param_1_0[n].Constants.ShaderRegister = desc_1_1.pParameters[n].Constants.ShaderRegister;
						break;

					case D3D12_ROOT_PARAMETER_TYPE_CBV:
					case D3D12_ROOT_PARAMETER_TYPE_SRV:
					case D3D12_ROOT_PARAMETER_TYPE_UAV:
						param_1_0[n].Descriptor.RegisterSpace = desc_1_1.pParameters[n].Descriptor.RegisterSpace;
						param_1_0[n].Descriptor.ShaderRegister = desc_1_1.pParameters[n].Descriptor.ShaderRegister;
						break;

					case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
					{
						const D3D12_ROOT_DESCRIPTOR_TABLE1& table_1_1 = desc_1_1.pParameters[n].DescriptorTable;

						const size_t size = sizeof(D3D12_DESCRIPTOR_RANGE) * table_1_1.NumDescriptorRanges;
						void* range = (size > 0 && SUCCEEDED(hr)) ? HeapAlloc(GetProcessHeap(), 0, size) : nullptr;
						if (size > 0 && range == NULL)
							hr = E_OUTOFMEMORY;
						D3D12_DESCRIPTOR_RANGE* range_1_0 = reinterpret_cast<D3D12_DESCRIPTOR_RANGE*>(range);

						if (SUCCEEDED(hr)) {
							for (uint32_t x = 0; x < table_1_1.NumDescriptorRanges; x ++) {
								__analysis_assume(size == sizeof(D3D12_DESCRIPTOR_RANGE) * table_1_1.NumDescriptorRanges);
								range_1_0[x].BaseShaderRegister = table_1_1.pDescriptorRanges[x].BaseShaderRegister;
								range_1_0[x].NumDescriptors = table_1_1.pDescriptorRanges[x].NumDescriptors;
								range_1_0[x].OffsetInDescriptorsFromTableStart = table_1_1.pDescriptorRanges[x].OffsetInDescriptorsFromTableStart;
								range_1_0[x].RangeType = table_1_1.pDescriptorRanges[x].RangeType;
								range_1_0[x].RegisterSpace = table_1_1.pDescriptorRanges[x].RegisterSpace;
							}
						}

						D3D12_ROOT_DESCRIPTOR_TABLE& table_1_0 = param_1_0[n].DescriptorTable;
						table_1_0.NumDescriptorRanges = table_1_1.NumDescriptorRanges;
						table_1_0.pDescriptorRanges = range_1_0;
						break;
					}
					default:
						break;
					}
				}
			}

			if (SUCCEEDED(hr)) {
				D3D12_ROOT_SIGNATURE_DESC desc_1_0 = {
					desc_1_1.NumParameters, param_1_0, desc_1_1.NumStaticSamplers, desc_1_1.pStaticSamplers, desc_1_1.Flags};
				hr = D3D12SerializeRootSignature(&desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, blob, err);
			}

			if (params) {
				for (int i = 0; i < desc_1_1.NumParameters; i ++) {
					if (desc_1_1.pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
						HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(const_cast<D3D12_DESCRIPTOR_RANGE*>(param_1_0[i].DescriptorTable.pDescriptorRanges)));
					}
				}
				HeapFree(GetProcessHeap(), 0, params);
			}
			return hr;
			break;
		}
		default:
			/* unknown version */
			break;
		}
	}
	else {
		/* unknown version */
	}
	
	return E_INVALIDARG;
}
