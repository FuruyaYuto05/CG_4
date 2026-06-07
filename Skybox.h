#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <string>
#include "DirectXCommon.h"
#include <array> 


class Skybox {
public:
    // 頂点構造体の定義（もし既存の構造体と競合する場合は適宜名前を変更してください）

    struct VertexData {
        std::array<float, 4> position; // std::array にすると一括代入が可能になります！
    };

    /// <summary>
    /// 初期化処理
    /// </summary>
    void Initialize(DirectXCommon* dxCommon, const std::string& textureFilePath);

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw(ID3D12GraphicsCommandList* commandList);

private:
    // DirectXCommonのポインタ保持用
    DirectXCommon* dxCommon_ = nullptr;

    // 頂点バッファ関連
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // インデックスバッファ関連
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    // テクスチャのSRVインデックス（テクスチャマネージャー等で管理する場合は調整してください）
    uint32_t textureIndex_ = 0;
};