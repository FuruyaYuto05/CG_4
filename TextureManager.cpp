#include "TextureManager.h"
#include "DirectXCommon.h"
#include "StringUtility.h"
#include <algorithm>

TextureManager* TextureManager::instance = nullptr;

uint32_t TextureManager::kSRVIndexTop = 1;

TextureManager* TextureManager::GetInstance()
{
    if (instance == nullptr) {
        instance = new TextureManager();
    }
    return instance;
}

void TextureManager::SetDirectXCommon(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
}

void TextureManager::Initialize()
{
    textureDatas.reserve(DirectXCommon::kMaxSRVCount);
}

void TextureManager::Finalize()
{
    delete instance;
    instance = nullptr;
}


// ============================
//   テクスチャ読み込み処理
// ============================
void TextureManager::LoadTexture(const std::string& filePath)
{
    // ★ DirectXCommon はインスタンス保持版を使う
    DirectXCommon* dxCommon = dxCommon_;

    // --- 重複読み込みチェック ---
    auto it = std::find_if(
        textureDatas.begin(),
        textureDatas.end(),
        [&](const TextureData& data) { return data.filePath == filePath; }
    );
    if (it != textureDatas.end()) {
        return;
    }

    assert(textureDatas.size() + kSRVIndexTop < DirectXCommon::kMaxSRVCount);

    // --- ファイル読み込み ---
    DirectX::ScratchImage image{};
    std::wstring filePathW = StringUtility::ConvertString(filePath);

    HRESULT hr;

    if (filePathW.ends_with(L".dds")) {
        hr = DirectX::LoadFromDDSFile(
            filePathW.c_str(),
            DirectX::DDS_FLAGS_NONE,
            nullptr,
            image
        );
    } else {
        hr = DirectX::LoadFromWICFile(
            filePathW.c_str(),
            DirectX::WIC_FLAGS_FORCE_SRGB,
            nullptr,
            image
        );
    }
    assert(SUCCEEDED(hr));

    auto meta = image.GetMetadata();

    assert(meta.width > 0);
    assert(meta.height > 0);
    assert(meta.arraySize > 0);

    DirectX::ScratchImage mipImages{};

    if (filePathW.ends_with(L".dds")) {
        // DDS(Cubemap)はGenerateMipMapsしない
        mipImages = std::move(image);
    } else {
        hr = DirectX::GenerateMipMaps(
            image.GetImages(),
            image.GetImageCount(),
            image.GetMetadata(),
            DirectX::TEX_FILTER_SRGB,
            0,
            mipImages
        );
        assert(SUCCEEDED(hr));
    }
    // --- テクスチャデータ追加 ---
    textureDatas.resize(textureDatas.size() + 1);
    TextureData& textureData = textureDatas.back();

    // --- データ書き込み ---
    textureData.filePath = filePath;
    textureData.metadata = mipImages.GetMetadata();
    textureData.resource = dxCommon->CreateTextureResource(textureData.metadata);

    uint32_t textureIndex =
        static_cast<uint32_t>(textureDatas.size() - 1);

    uint32_t srvIndex = textureIndex + kSRVIndexTop;

    textureData.srvHandleCPU =
        dxCommon->GetSRVCPUDescriptorHandle(srvIndex);

    textureData.srvHandleGPU =
        dxCommon->GetSRVGPUDescriptorHandle(srvIndex);

    // --- SRV の生成 ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureData.metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    // ★ ここにスライドの内容を書きます！
    if (textureData.metadata.IsCubemap()) {
        // キューブマップ（DDS）用の設定
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = UINT_MAX;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    } else {
        // 今まで通り通常の2Dテクスチャ（PNGなど）用の設定
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(textureData.metadata.mipLevels);
    }

    dxCommon->GetDevice()->CreateShaderResourceView(
        textureData.resource.Get(),
        &srvDesc,
        textureData.srvHandleCPU
    );

    // --- GPU へテクスチャデータ転送 ---
    dxCommon->UploadTextureData(textureData.resource, mipImages);
}


uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath)
{
    // 読み込み済みテクスチャを検索
    auto it = std::find_if(
        textureDatas.begin(),
        textureDatas.end(),
        [&](const TextureData& data) { return data.filePath == filePath; }
    );

    // 見つかった？
    if (it != textureDatas.end()) {
        // インデックス = it - begin
        uint32_t textureIndex =
            static_cast<uint32_t>(std::distance(textureDatas.begin(), it));

        return textureIndex;
    }

    // 見つからなかったら 0 を返して停止（assert）
    assert(0);
    return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex)
{
    // テクスチャ番号が範囲内かチェック
    assert(textureIndex < textureDatas.size());

    // 対応するテクスチャデータを取得
    TextureData& textureData = textureDatas[textureIndex];

    return textureData.srvHandleGPU;
}

// メタデータを取得
const DirectX::TexMetadata& TextureManager::GetMetaData(uint32_t textureIndex)
{
    // 範囲外指定違反チェック
    assert(textureIndex < textureDatas.size());

    // テクスチャデータの参照を取得
    TextureData& textureData = textureDatas[textureIndex];

    // メタデータを返す
    return textureData.metadata;
}