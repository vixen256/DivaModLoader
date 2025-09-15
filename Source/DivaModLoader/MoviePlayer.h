#pragma once

struct MoviePlayer {
    static void preInit();
    static void init();
    static void d3dInit(IDXGISwapChain *swapChain, ID3D11Device* device, ID3D11DeviceContext* deviceContext);
};