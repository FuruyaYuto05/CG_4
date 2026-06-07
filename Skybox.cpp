#include "Skybox.h"
#include "TextureManager.h"
#include <algorithm>
#include <cassert>

void Skybox::Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;

    // ==========================================
    // 1. 頂点データの作成
    // ==========================================
    VertexData vertexData[24];

    // --- 右面 (Right) 描画インデックスは [0,1,2][2,1,3] で内側を向く ---
    vertexData[0].position = { 1.0f,  1.0f,  1.0f, 1.0f };
    vertexData[1].position = { 1.0f,  1.0f, -1.0f, 1.0f };
    vertexData[2].position = { 1.0f, -1.0f,  1.0f, 1.0f };
    vertexData[3].position = { 1.0f, -1.0f, -1.0f, 1.0f };

    // --- 左面 (Left) 描画インデックスは [4,5,6][6,5,7] ---
    vertexData[4].position = { -1.0f,  1.0f, -1.0f, 1.0f };
    vertexData[5].position = { -1.0f,  1.0f,  1.0f, 1.0f };
    vertexData[6].position = { -1.0f, -1.0f, -1.0f, 1.0f };
    vertexData[7].position = { -1.0f, -1.0f,  1.0f, 1.0f };

    // --- 前面 (Front) 描画インデックスは [8,9,10][10,9,11] ---
    vertexData[8].position = { -1.0f,  1.0f,  1.0f, 1.0f };
    vertexData[9].position = { 1.0f,  1.0f,  1.0f, 1.0f };
    vertexData[10].position = { -1.0f, -1.0f,  1.0f, 1.0f };
    vertexData[11].position = { 1.0f, -1.0f,  1.0f, 1.0f };

    // --- 後面 (Back) 描画インデックスは [12,13,14][14,13,15] ---
    vertexData[12].position = { 1.0f,  1.0f, -1.0f, 1.0f };
    vertexData[13].position = { -1.0f,  1.0f, -1.0f, 1.0f };
    vertexData[14].position = { 1.0f, -1.0f, -1.0f, 1.0f };
    vertexData[15].position = { -1.0f, -1.0f, -1.0f, 1.0f };

    // --- 上面 (Top) 描画インデックスは [16,17,18][18,17,19] ---
    vertexData[16].position = { -1.0f,  1.0f, -1.0f, 1.0f };
    vertexData[17].position = { 1.0f,  1.0f, -1.0f, 1.0f };
    vertexData[18].position = { -1.0f,  1.0f,  1.0f, 1.0f };
    vertexData[19].position = { 1.0f,  1.0f,  1.0f, 1.0f };

    // --- 下面 (Bottom) 描画インデックスは [20,21,22][22,21,23] ---
    vertexData[20].position = { -1.0f, -1.0f,  1.0f, 1.0f };
    vertexData[21].position = { 1.0f, -1.0f,  1.0f, 1.0f };
    vertexData[22].position = { -1.0f, -1.0f, -1.0f, 1.0f };
    vertexData[23].position = { 1.0f, -1.0f, -1.0f, 1.0f };

    // 頂点バッファの生成とデータ転送
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 24);
    VertexData* vertexMap = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexMap));
    std::copy(std::begin(vertexData), std::end(vertexData), vertexMap);
    vertexResource_->Unmap(0, nullptr);

    // 頂点バッファビューの設定
  // 頂点バッファビューの設定（変数名の一番後ろに _ を付けます）
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 24;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // ==========================================
    // 2. インデックスデータの作成（内側を向く設定）
    // ==========================================
    uint32_t indexData[36] = {
        0, 1, 2,   2, 1, 3,   // 右面
        4, 5, 6,   6, 5, 7,   // 左面
        8, 9, 10,  10,9, 11,  // 前面
        12,13,14,  14,13,15,  // 後面
        16,17,18,  18,17,19,  // 上面
        20,21,22,  22,21,23   // 下面
    };

    // インデックスバッファの生成とデータ転送
    indexResource_ = dxCommon_->CreateBufferResource(sizeof(uint32_t) * 36);
    uint32_t* indexMap = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexMap));
    std::copy(std::begin(indexData), std::end(indexData), indexMap);
    indexResource_->Unmap(0, nullptr);

    // インデックスバッファビューの設定
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(uint32_t) * 36;
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    // ==========================================
    // 3. DDSテクスチャの読み込み
    // ==========================================
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    textureIndex_ = TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath);
}

void Skybox::Draw(ID3D12GraphicsCommandList* commandList)
{
    assert(commandList);

    // 頂点バッファとインデックスバッファのセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // テクスチャ（キューブマップ）のSRVをルートパラメータにセット
    // ※引数のインデックス（例：1）や、DescriptorHeapのハンドル取得方法はお使いのシステムに合わせて調整してください
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureIndex_);
    commandList->SetGraphicsRootDescriptorTable(1, srvHandle); // ルートパラメータ番号は仮に「1」としています

    // インデックスを用いた描画（36インデックス、1インスタンス）
    commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
}