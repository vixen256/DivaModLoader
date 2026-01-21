#include "MoviePlayer.h"
#include "MoviePlayer_vs.fxh";
#include "MoviePlayer_bt601_full.fxh"
#include "MoviePlayer_bt601_limited.fxh"
#include "MoviePlayer_bt709_full.fxh"
#include "MoviePlayer_bt709_limited.fxh"
#include "MoviePlayer_bt2020_full.fxh"
#include "MoviePlayer_bt2020_limited.fxh"

#include "Utilities.h"
#include "Types.h"
#include "Context.h"

bool vulkan = false;

IDXGISwapChain *d3d11SwapChain;
ID3D11Device* d3d11Device;
ID3D11DeviceContext* d3d11DeviceContext;

ID3D11VertexShader* shader_vs;
ID3D11PixelShader* shader_bt601_full;
ID3D11PixelShader* shader_bt601_limited;
ID3D11PixelShader* shader_bt709_full;
ID3D11PixelShader* shader_bt709_limited;
ID3D11PixelShader* shader_bt2020_full;
ID3D11PixelShader* shader_bt2020_limited;

VkInstance vkInstance;
VkPhysicalDevice vkPhysDevice;
VkDevice vkDevice;
uint32_t vkVersion;

std::vector<char*> vkInstanceExtensions;
std::vector<char*> vkDeviceExtensions;
VkPhysicalDeviceFeatures2 vkDeviceFeatures;
std::vector<AVVulkanDeviceQueueFamily> vkQueueFamilies;

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct Mat4 {
    Vec4 x;
    Vec4 y;
    Vec4 z;
    Vec4 w;
};

struct DirectXTexture {
    int32_t ref_count;
    DirectXTexture* free_next;
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* resource_view;
    int32_t format;
    int32_t internal_format;
    int32_t flags;
    int32_t width;
    int32_t height;
    int32_t mip_levels;
};

struct Texture {
    int32_t ref_count;
    int32_t id;
    int32_t flags;
    int16_t width;
    int16_t height;
    uint32_t target;
    uint32_t format;
    int32_t max_mipmap;
    int32_t size;
    int32_t unk_0x20;
    DirectXTexture* dx_texture;
};

class TaskInterface {
public:
    virtual ~TaskInterface() {}
    virtual bool Init() = 0;
    virtual bool Loop() = 0;
    virtual bool Destroy() = 0;
    virtual bool Display() = 0;
    virtual bool Basic() = 0;
};

struct Task : public TaskInterface {
    int32_t priority;
    Task* parent;
    int32_t op;
    int32_t state;
    int32_t request;
    int32_t nextOp;
    int32_t nextState;
    bool resetOnDestroy;
    bool isFrameDependent;
    char name[32];
    uint32_t base_calc_time;
    uint32_t calc_time;
    uint32_t calc_time_max;
    uint32_t disp_time;
    uint32_t disp_time_max;
};

struct SpriteArgs {
    uint32_t kind;
    int32_t id;
    uint8_t color[4];
    int32_t attr;
    int32_t blend;
    int32_t index;
    int32_t layer;
    int32_t priority;
    int32_t resolution_mode_screen;
    int32_t resolution_mode_sprite;
    Vec3 center;
    Vec3 trans;
    Vec3 scale;
    Vec3 rot;
    Vec2 skew_angle;
    Mat4 mat;
    Texture* texture;
    int32_t shader;
    int32_t field_AC;
    Mat4 transform;
    bool field_F0;
    void* vertex_array;
    size_t num_vertex;
    int32_t field_108;
    void* field_110;
    bool set_viewport;
    Vec4 viewport;
    uint32_t flags;
    Vec2 sprite_size;
    int32_t field_138;
    Vec2 texture_pos;
    Vec2 texture_size;
    SpriteArgs* next;
    DirectXTexture* dx_texture;
};

FUNCTION_PTR(Texture*, __fastcall, TextureLoadTex2D, 0x1405F0720, uint32_t id, int32_t format, uint32_t width, uint32_t, int32_t mip_levels, void** data, int32_t, bool generate_mips);
FUNCTION_PTR(void, __fastcall, TextureRelease, 0x1405F0950, Texture*);
FUNCTION_PTR(void, __fastcall, DefaultSpriteArgs, 0x1405B78D0, SpriteArgs* args);
FUNCTION_PTR(SpriteArgs*, __fastcall, put_sprite, 0x1405B49C0, SpriteArgs* args);
FUNCTION_PTR(bool, __fastcall, DelTask, 0x1529F3780, Task* task);
FUNCTION_PTR(bool, __fastcall, ResolveFilePath, 0x1402A5330, prj::string* in, prj::string* out);

struct FFmpegPlayer {
    enum class Status {
        NotInitialized = 0,
        Playing,
        Shutdown,
    };

    enum class Shader {
        BT601_FULL,
        BT601_LIMITED,
        BT709_FULL,
        BT709_LIMITED,
        BT2020_FULL,
        BT2020_LIMITED,
    };

    AVFormatContext* input_ctx;
    AVCodecContext* decoder_ctx;
    AVStream* video;
    AVFrame* frame;
    int32_t stream_index;
    double position;
    double next_frame;
    Status status;
    Shader shader;
    bool frame_updated;

    ID3D11SamplerState* sampler;

    ID3D11Texture2D* nv12_texture;
    ID3D11ShaderResourceView* luminance_view;
    ID3D11ShaderResourceView* chrominance_view;

    ID3D11Texture2D* staging_nv12_texture;
    ID3D11ShaderResourceView* staging_luminance_view;
    ID3D11ShaderResourceView* staging_chrominance_view;
    
    Texture* game_texture;
    ID3D11RenderTargetView* render_target;

    FFmpegPlayer() {
        this->input_ctx = nullptr;
        this->decoder_ctx = nullptr;
        this->video = nullptr;
        this->frame = nullptr;
        this->sampler = nullptr;
        this->nv12_texture = nullptr;
        this->luminance_view = nullptr;
        this->chrominance_view = nullptr;
        this->staging_nv12_texture = nullptr;
        this->staging_luminance_view = nullptr;
        this->staging_chrominance_view = nullptr;
        this->game_texture = nullptr;
        this->render_target = nullptr;

        this->stream_index = 0;
        this->position = 0.0;
        this->next_frame = 0.0;
        this->status = Status::NotInitialized;
        this->shader = Shader::BT709_FULL;
        this->frame_updated = false;
    }

    void Reset() {
        if (vulkan && this->decoder_ctx != nullptr && this->decoder_ctx->hw_device_ctx != nullptr) {
            AVHWDeviceContext* hw_device_ctx = (AVHWDeviceContext*)this->decoder_ctx->hw_device_ctx->data;
            AVVulkanDeviceContext* vk_device_ctx = (AVVulkanDeviceContext*)hw_device_ctx->hwctx;

            vk_device_ctx->inst = VK_NULL_HANDLE;
            vk_device_ctx->phys_dev = VK_NULL_HANDLE;
            vk_device_ctx->act_dev = VK_NULL_HANDLE;
        }

        if (this->input_ctx != nullptr) {
            avformat_close_input(&this->input_ctx);
            this->input_ctx = nullptr;
            this->video = nullptr;
        }
        if (this->decoder_ctx != nullptr) {
            avcodec_free_context(&this->decoder_ctx);
            this->decoder_ctx = nullptr;
        }
        if (this->frame != nullptr) {
            av_frame_free(&this->frame);
            this->frame = nullptr;
        }
        if (this->game_texture != nullptr) {
            TextureRelease(this->game_texture);
            this->game_texture = nullptr;
        }
        if (this->sampler != nullptr) {
            this->sampler->Release();
            this->sampler = nullptr;
        }
        if (this->nv12_texture != nullptr) {
            this->nv12_texture->Release();
            this->nv12_texture = nullptr;
        }
        if (this->luminance_view != nullptr) {
            this->luminance_view->Release();
            this->luminance_view = nullptr;
        }
        if (this->chrominance_view != nullptr) {
            this->chrominance_view->Release();
            this->chrominance_view = nullptr;
        }
        if (this->staging_nv12_texture != nullptr) {
            this->staging_nv12_texture->Release();
            this->staging_nv12_texture = nullptr;
        }
        if (this->staging_luminance_view != nullptr) {
            this->staging_luminance_view->Release();
            this->staging_luminance_view = nullptr;
        }
        if (this->staging_chrominance_view != nullptr) {
            this->staging_chrominance_view->Release();
            this->staging_chrominance_view = nullptr;
        }
        if (this->render_target != nullptr) {
            this->render_target->Release();
            this->render_target = nullptr;
        }

        this->stream_index = 0;
        this->status = FFmpegPlayer::Status::NotInitialized;
        this->position = 0.0;
        this->next_frame = 0.0;
        this->shader = FFmpegPlayer::Shader::BT709_FULL;
        this->frame_updated = false;
    }
};

struct TaskMovie : Task {
    enum class State : int32_t {
        Wait = 0,
        Init = 1,
        Disp = 2,
        Stop = 3,
        Shutdown = 4,
    };

    enum class DispType : int32_t {
        None = 0,
        Sprite = 1,
        Texture = 2,
    };

    struct SprParams {
        Vec4 rect;
        uint32_t resolution_mode;
        float scale;
        uint32_t unk_18;
        uint32_t index;
        uint32_t priority;
        uint32_t unk_24;
    };

    struct TextureInfo {
        Texture* g_texture_y;
        Texture* g_texture_uv;
        void* unk_10;
        int32_t width;
        int32_t height;
    };

    prj::string path;
    DispType disp_type;
    SprParams params;
    void* unk_B8;
    State state;
    void* player;
    bool paused;
    bool wait_play;
    bool is_ffmpeg; // Added in padding
    int64_t unk_D8;
    int32_t unk_E0;
    int64_t time;
    int64_t offset;
    int32_t index;
};

FFmpegPlayer ffmpeg_players[2];

AVPixelFormat
get_format(AVCodecContext* decoder_ctx, const AVPixelFormat* pixel_formats) {
    const std::set<AVPixelFormat> known_formats = { vulkan ? AV_PIX_FMT_VULKAN : AV_PIX_FMT_D3D11, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10LE };
    std::set<AVPixelFormat> formats = {};
    while (*pixel_formats != AV_PIX_FMT_NONE) {
        if (known_formats.find(*pixel_formats) != known_formats.end()) formats.insert(*pixel_formats);
        pixel_formats++;
    }

    if (decoder_ctx->hw_device_ctx != nullptr && formats.find(AV_PIX_FMT_VULKAN) != formats.end()) return AV_PIX_FMT_VULKAN;
    else if (decoder_ctx->hw_device_ctx != nullptr && formats.find(AV_PIX_FMT_D3D11) != formats.end()) return AV_PIX_FMT_D3D11;
    else if (formats.find(AV_PIX_FMT_YUV420P) != formats.end()) return AV_PIX_FMT_YUV420P;
    else if (formats.find(AV_PIX_FMT_YUV420P10LE) != formats.end()) return AV_PIX_FMT_YUV420P10LE;
    else return AV_PIX_FMT_NONE;
}

HOOK(TaskMovie*, __fastcall, TaskMovieInit, 0x140454660, TaskMovie* movie) {
    movie->is_ffmpeg = false;
    return originalTaskMovieInit(movie);
}

HOOK(bool, __fastcall, TaskMovieCheckDisp, 0x1569B1520, TaskMovie* movie) {
    if (!movie->is_ffmpeg) return originalTaskMovieCheckDisp(movie);
    if (!movie->wait_play) return false;
    auto player = &ffmpeg_players[movie->index];

    return player->status == FFmpegPlayer::Status::Playing || player->status == FFmpegPlayer::Status::Shutdown;
}

HOOK(bool, __fastcall, TaskMovieCtrl, 0x140454890, TaskMovie* movie) {
    if (!movie->is_ffmpeg) return originalTaskMovieCtrl(movie);

    auto player = &ffmpeg_players[movie->index];
    switch (player->status) {
    case FFmpegPlayer::Status::NotInitialized: {
        int32_t res = 0;
        player->Reset();

        player->input_ctx = avformat_alloc_context();
        res = avformat_open_input(&player->input_ctx, movie->path.c_str(), nullptr, nullptr);
        if (res < 0 || player->input_ctx == nullptr) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        res = avformat_find_stream_info(player->input_ctx, nullptr);
        if (res < 0) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        const AVCodec* decoder;
        res = av_find_best_stream(player->input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (res < 0 || decoder == nullptr) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        player->stream_index = res;
        player->video = player->input_ctx->streams[player->stream_index];

        int32_t i = 0;
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
        while (config != nullptr) {
            if (config->device_type == (vulkan ? AV_HWDEVICE_TYPE_VULKAN : AV_HWDEVICE_TYPE_D3D11VA)) break;
            config = avcodec_get_hw_config(decoder, i);
            i++;
        }

        player->decoder_ctx = avcodec_alloc_context3(decoder);
        if (player->decoder_ctx == nullptr) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        res = avcodec_parameters_to_context(player->decoder_ctx, player->video->codecpar);
        if (res < 0) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        player->decoder_ctx->get_format = get_format;

        if (config == nullptr) {
        }
        else if (vulkan) {
            AVBufferRef* hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
            AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)(hw_device_ctx->data);
            if (device_ctx == nullptr) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            AVVulkanDeviceContext* vulkan_device_ctx = (AVVulkanDeviceContext*)(device_ctx->hwctx);
            if (vulkan_device_ctx == nullptr) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            vulkan_device_ctx->get_proc_addr = vkGetInstanceProcAddr;
            vulkan_device_ctx->inst = vkInstance;
            vulkan_device_ctx->phys_dev = vkPhysDevice;
            vulkan_device_ctx->act_dev = vkDevice;
            vulkan_device_ctx->device_features = vkDeviceFeatures;
            vulkan_device_ctx->enabled_inst_extensions = vkInstanceExtensions.data();
            vulkan_device_ctx->nb_enabled_inst_extensions = vkInstanceExtensions.size();
            vulkan_device_ctx->enabled_dev_extensions = vkDeviceExtensions.data();
            vulkan_device_ctx->nb_enabled_dev_extensions = vkDeviceExtensions.size();
            memcpy(&vulkan_device_ctx->qf, vkQueueFamilies.data(), sizeof(AVVulkanDeviceQueueFamily) * vkQueueFamilies.size());
            vulkan_device_ctx->nb_qf = vkQueueFamilies.size();

            res = av_hwdevice_ctx_init(hw_device_ctx);
            if (res < 0) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            player->decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        }
        else {
            AVBufferRef* hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
            AVHWDeviceContext* device_ctx = (AVHWDeviceContext*)(hw_device_ctx->data);
            if (device_ctx == nullptr) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            AVD3D11VADeviceContext* d3d11va_device_ctx = (AVD3D11VADeviceContext*)(device_ctx->hwctx);
            if (d3d11va_device_ctx == nullptr) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            d3d11va_device_ctx->device = d3d11Device;
            d3d11va_device_ctx->device_context = d3d11DeviceContext;
            res = av_hwdevice_ctx_init(hw_device_ctx);
            if (res < 0) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            player->decoder_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        }

        res = avcodec_open2(player->decoder_ctx, decoder, nullptr);
        if (res < 0) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        res = av_seek_frame(player->input_ctx, player->stream_index, 0, 0);
        if (res < 0) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        if (player->decoder_ctx->color_primaries == AVCOL_PRI_BT470M || player->decoder_ctx->color_primaries == AVCOL_PRI_BT470BG ||
            player->decoder_ctx->color_primaries == AVCOL_PRI_SMPTE170M || player->decoder_ctx->color_primaries == AVCOL_PRI_SMPTE240M) {
            if (player->decoder_ctx->color_range == AVCOL_RANGE_MPEG) player->shader = FFmpegPlayer::Shader::BT601_LIMITED;
            else if (player->decoder_ctx->color_range == AVCOL_RANGE_JPEG) player->shader = FFmpegPlayer::Shader::BT601_FULL;
            else player->shader = FFmpegPlayer::Shader::BT601_LIMITED;
        }
        else if (player->decoder_ctx->color_primaries == AVCOL_PRI_BT709) {
            if (player->decoder_ctx->color_range == AVCOL_RANGE_MPEG) player->shader = FFmpegPlayer::Shader::BT709_LIMITED;
            else if (player->decoder_ctx->color_range == AVCOL_RANGE_JPEG) player->shader = FFmpegPlayer::Shader::BT709_FULL;
            else player->shader = FFmpegPlayer::Shader::BT709_FULL;
        }
        else if (player->decoder_ctx->color_primaries == AVCOL_PRI_BT2020) {
            if (player->decoder_ctx->color_range == AVCOL_RANGE_MPEG) player->shader = FFmpegPlayer::Shader::BT2020_LIMITED;
            else if (player->decoder_ctx->color_range == AVCOL_RANGE_JPEG) player->shader = FFmpegPlayer::Shader::BT2020_FULL;
            else player->shader = FFmpegPlayer::Shader::BT2020_FULL;
        }
        else if (player->decoder_ctx->height < 720) {
            if (player->decoder_ctx->color_range == AVCOL_RANGE_MPEG) player->shader = FFmpegPlayer::Shader::BT601_LIMITED;
            else if (player->decoder_ctx->color_range == AVCOL_RANGE_JPEG) player->shader = FFmpegPlayer::Shader::BT601_FULL;
            else player->shader = FFmpegPlayer::Shader::BT601_LIMITED;
        }
        else {
            if (player->decoder_ctx->color_range == AVCOL_RANGE_MPEG) player->shader = FFmpegPlayer::Shader::BT709_LIMITED;
            else if (player->decoder_ctx->color_range == AVCOL_RANGE_JPEG) player->shader = FFmpegPlayer::Shader::BT709_FULL;
            else player->shader = FFmpegPlayer::Shader::BT709_FULL;
        }

        HRESULT hr;

        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.MipLODBias = 0.0;
        sampler_desc.MaxAnisotropy = 1;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_desc.BorderColor[0] = 0.0;
        sampler_desc.BorderColor[1] = 0.5;
        sampler_desc.BorderColor[2] = 0.5;
        sampler_desc.BorderColor[3] = 0.0;
        sampler_desc.MinLOD = 0.0;
        sampler_desc.MaxLOD = 1.0;

        hr = d3d11Device->CreateSamplerState(&sampler_desc, &player->sampler);
        if (FAILED(hr)) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        if (player->decoder_ctx->pix_fmt == AV_PIX_FMT_YUV420P) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = player->decoder_ctx->width;
            desc.Height = player->decoder_ctx->height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_NV12;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;

            hr = d3d11Device->CreateTexture2D(&desc, nullptr, &player->nv12_texture);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            hr = d3d11Device->CreateTexture2D(&desc, nullptr, &player->staging_nv12_texture);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format = DXGI_FORMAT_R8_UNORM;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = -1;
            srv_desc.Texture2D.MostDetailedMip = 0;

            hr = d3d11Device->CreateShaderResourceView(player->nv12_texture, &srv_desc, &player->luminance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            hr = d3d11Device->CreateShaderResourceView(player->staging_nv12_texture, &srv_desc, &player->staging_luminance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;

            hr = d3d11Device->CreateShaderResourceView(player->nv12_texture, &srv_desc, &player->chrominance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            hr = d3d11Device->CreateShaderResourceView(player->staging_nv12_texture, &srv_desc, &player->staging_chrominance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }
        }
        else if (player->decoder_ctx->pix_fmt == AV_PIX_FMT_YUV420P10LE) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = player->decoder_ctx->width;
            desc.Height = player->decoder_ctx->height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_P010;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;

            hr = d3d11Device->CreateTexture2D(&desc, nullptr, &player->nv12_texture);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            hr = d3d11Device->CreateTexture2D(&desc, nullptr, &player->staging_nv12_texture);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format = DXGI_FORMAT_R16_UNORM;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = -1;
            srv_desc.Texture2D.MostDetailedMip = 0;

            hr = d3d11Device->CreateShaderResourceView(player->nv12_texture, &srv_desc, &player->luminance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            hr = d3d11Device->CreateShaderResourceView(player->staging_nv12_texture, &srv_desc, &player->staging_luminance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            srv_desc.Format = DXGI_FORMAT_R16G16_UNORM;

            hr = d3d11Device->CreateShaderResourceView(player->nv12_texture, &srv_desc, &player->chrominance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }

            hr = d3d11Device->CreateShaderResourceView(player->staging_nv12_texture, &srv_desc, &player->staging_chrominance_view);
            if (FAILED(hr)) {
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }
        }

        player->game_texture = TextureLoadTex2D(0x393939 + movie->index, 6, player->decoder_ctx->width, player->decoder_ctx->height, 0, nullptr, 0, false);

        hr = d3d11Device->CreateRenderTargetView(player->game_texture->dx_texture->texture, nullptr, &player->render_target);
        if (FAILED(hr)) {
            movie->state = TaskMovie::State::Shutdown;
            player->status = FFmpegPlayer::Status::Shutdown;
            break;
        }

        player->position = 0.0;
        player->next_frame = 0.0;

        movie->state = TaskMovie::State::Disp;
        player->status = FFmpegPlayer::Status::Playing;
    }; break;
    case FFmpegPlayer::Status::Playing: {
        if (movie->time == -1) break;

        player->position = ((double)movie->time - (double)movie->offset) / 1000.0 / 1000.0;
        while (player->position >= player->next_frame && player->next_frame < (double)player->input_ctx->duration * av_q2d(AV_TIME_BASE_Q) && !movie->paused && !movie->wait_play) {
            int32_t res;

            AVPacket* packet = av_packet_alloc();
            res = av_read_frame(player->input_ctx, packet);
            if (res < 0) {
                av_packet_free(&packet);
                movie->state = TaskMovie::State::Shutdown;
                player->status = FFmpegPlayer::Status::Shutdown;
                break;
            }
            if (packet->stream_index != player->stream_index) {
                av_packet_free(&packet);
                continue;
            }

            res = avcodec_send_packet(player->decoder_ctx, packet);
            if (res < 0) {
                av_packet_free(&packet);
                if (res == AVERROR(EAGAIN)) {
                    continue;
                }
                else {
                    movie->state = TaskMovie::State::Shutdown;
                    player->status = FFmpegPlayer::Status::Shutdown;
                    break;
                }
            }

            player->frame_updated = false;

            if (player->frame != nullptr) av_frame_free(&player->frame);
            player->frame = av_frame_alloc();
            res = avcodec_receive_frame(player->decoder_ctx, player->frame);
            if (res < 0) {
                av_frame_free(&player->frame);
                av_packet_free(&packet);
                if (res == AVERROR(EAGAIN)) {
                    continue;
                }
                else {
                    movie->state = TaskMovie::State::Shutdown;
                    player->status = FFmpegPlayer::Status::Shutdown;
                    break;
                }
            }

            player->frame_updated = true;
            player->next_frame = ((double)packet->duration + (double)player->frame->pts) * av_q2d(player->video->time_base);

            av_packet_free(&packet);
        }
    }; break;
    case FFmpegPlayer::Status::Shutdown: {
    }; break;
    }

    return false;
}

HOOK(void, __fastcall, TaskMovieCtrlPlayer, 0x1404551B0, TaskMovie* movie) {
    if (!movie->is_ffmpeg) return originalTaskMovieCtrlPlayer(movie);

    auto player = &ffmpeg_players[movie->index];
    if (player->input_ctx == nullptr) return;

    double position = ((double)movie->time - (double)movie->offset) / 1000.0 / 1000.0;
    if (position < 0.0) position = 0.0;

    avformat_seek_file(player->input_ctx, player->stream_index, 0, (int64_t)(position / av_q2d(player->video->time_base)), (int64_t)(position / av_q2d(player->video->time_base)), 0);
    player->position = position;
    player->next_frame = 0.0;
}

HOOK(void, __fastcall, TaskMovieDisp, 0x1404549D0, TaskMovie* movie) {
    if (!movie->is_ffmpeg) return originalTaskMovieDisp(movie);

    auto player = &ffmpeg_players[movie->index];
    if (movie->state != TaskMovie::State::Disp || movie->disp_type != TaskMovie::DispType::Sprite) return;
    if (player->status != FFmpegPlayer::Status::Playing || player->game_texture == nullptr || player->frame == nullptr) return;

    if (player->frame_updated) {
        d3d11DeviceContext->VSSetShader(shader_vs, nullptr, 0);
        switch (player->shader) {
        case FFmpegPlayer::Shader::BT601_FULL: d3d11DeviceContext->PSSetShader(shader_bt601_full, nullptr, 0); break;
        case FFmpegPlayer::Shader::BT601_LIMITED: d3d11DeviceContext->PSSetShader(shader_bt601_limited, nullptr, 0); break;
        case FFmpegPlayer::Shader::BT709_FULL: d3d11DeviceContext->PSSetShader(shader_bt709_full, nullptr, 0); break;
        case FFmpegPlayer::Shader::BT709_LIMITED: d3d11DeviceContext->PSSetShader(shader_bt709_limited, nullptr, 0); break;
        case FFmpegPlayer::Shader::BT2020_FULL: d3d11DeviceContext->PSSetShader(shader_bt2020_full, nullptr, 0); break;
        case FFmpegPlayer::Shader::BT2020_LIMITED: d3d11DeviceContext->PSSetShader(shader_bt2020_limited, nullptr, 0); break;
        };

        D3D11_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = player->decoder_ctx->width;
        viewport.Height = player->decoder_ctx->height;

        d3d11DeviceContext->RSSetViewports(1, &viewport);
        d3d11DeviceContext->OMSetRenderTargets(1, &player->render_target, nullptr);
        d3d11DeviceContext->PSSetSamplers(0, 1, &player->sampler);

        if (player->frame->format == AV_PIX_FMT_VULKAN) {
            AVVkFrame* vk_frame = (AVVkFrame*)player->frame->data[0];
            AVHWDeviceContext* hw_device_ctx = (AVHWDeviceContext*)player->decoder_ctx->hw_device_ctx->data;
            AVHWFramesContext* hw_frames_ctx = (AVHWFramesContext*)player->decoder_ctx->hw_frames_ctx->data;
            AVVulkanDeviceContext* vk_device_ctx = (AVVulkanDeviceContext*)hw_device_ctx->hwctx;
            IDXGIVkInteropDevice1* vkInteropDevice = nullptr;
            d3d11Device->QueryInterface(&vkInteropDevice);

            switch (hw_frames_ctx->sw_format) {
            case AV_PIX_FMT_NV12: {
                ID3D11Texture2D* texture;
                ID3D11ShaderResourceView* luminance_view;
                ID3D11ShaderResourceView* chrominance_view;

                D3D11_TEXTURE2D_DESC1 desc = {};
                desc.Width = player->decoder_ctx->width;
                desc.Height = player->decoder_ctx->height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_NV12;
                desc.SampleDesc.Count = 1;
                desc.SampleDesc.Quality = 0;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;

                HRESULT hr = vkInteropDevice->CreateTexture2DFromVkImage(&desc, vk_frame->img[0], &texture);
                if (FAILED(hr)) {
                    break;
                }
                
                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Format = DXGI_FORMAT_R8_UNORM;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MipLevels = -1;
                srv_desc.Texture2D.MostDetailedMip = 0;

                hr = d3d11Device->CreateShaderResourceView(texture, &srv_desc, &luminance_view);
                if (FAILED(hr)) {
                    break;
                }

                srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;

                hr = d3d11Device->CreateShaderResourceView(texture, &srv_desc, &chrominance_view);
                if (FAILED(hr)) {
                    break;
                }

                d3d11DeviceContext->PSSetShaderResources(0, 1, &luminance_view);
                d3d11DeviceContext->PSSetShaderResources(1, 1, &chrominance_view);
                d3d11DeviceContext->Draw(6, 0);
                
                chrominance_view->Release();
                luminance_view->Release();
                texture->Release();
            }; break;
            case AV_PIX_FMT_P010LE: {
                ID3D11Texture2D* texture;
                ID3D11ShaderResourceView* luminance_view;
                ID3D11ShaderResourceView* chrominance_view;

                D3D11_TEXTURE2D_DESC1 desc = {};
                desc.Width = player->decoder_ctx->width;
                desc.Height = player->decoder_ctx->height;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_P010;
                desc.SampleDesc.Count = 1;
                desc.SampleDesc.Quality = 0;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                desc.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;

                HRESULT hr = vkInteropDevice->CreateTexture2DFromVkImage(&desc, vk_frame->img[0], &texture);
                if (FAILED(hr)) {
                    break;
                }

                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Format = DXGI_FORMAT_R16_UNORM;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MipLevels = -1;
                srv_desc.Texture2D.MostDetailedMip = 0;

                hr = d3d11Device->CreateShaderResourceView(texture, &srv_desc, &luminance_view);
                if (FAILED(hr)) {
                    break;
                }

                srv_desc.Format = DXGI_FORMAT_R16G16_UNORM;

                hr = d3d11Device->CreateShaderResourceView(texture, &srv_desc, &chrominance_view);
                if (FAILED(hr)) {
                    break;
                }

                d3d11DeviceContext->PSSetShaderResources(0, 1, &luminance_view);
                d3d11DeviceContext->PSSetShaderResources(1, 1, &chrominance_view);
                d3d11DeviceContext->Draw(6, 0);

                chrominance_view->Release();
                luminance_view->Release();
                texture->Release();
            }; break;
            default: {
            }; break;
            }
        }
        else if (player->frame->format == AV_PIX_FMT_D3D11) {
            d3d11DeviceContext->CopySubresourceRegion(player->nv12_texture, 0, 0, 0, 0, (ID3D11Texture2D*)player->frame->data[0], (uint32_t)player->frame->data[1], nullptr);
            d3d11DeviceContext->PSSetShaderResources(0, 1, &player->luminance_view);
            d3d11DeviceContext->PSSetShaderResources(1, 1, &player->chrominance_view);
            d3d11DeviceContext->Draw(6, 0);
        }
        else if (player->frame->format == AV_PIX_FMT_YUV420P) {
            D3D11_MAPPED_SUBRESOURCE map;
            HRESULT hr = d3d11DeviceContext->Map(player->staging_nv12_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            if (FAILED(hr)) return;

            for (int i = 0; i < player->frame->height; i++)
                memcpy((uint8_t*)map.pData + i * map.RowPitch, (uint8_t*)player->frame->data[0] + i * player->frame->linesize[0], std::min((uint32_t)player->frame->linesize[0], map.RowPitch));

            const __m128i filter = _mm_set_epi8(7, 7 + 8, 6, 6 + 8, 5, 5 + 8, 4, 4 + 8, 3, 3 + 8, 2, 2 + 8, 1, 1 + 8, 0, 0 + 8);
            for (int i = 0; i < player->frame->height / 2; i++){
                int u_offset = i * player->frame->linesize[1];
                int v_offset = i * player->frame->linesize[2];
                int tex_offset = (i + player->frame->height) * map.RowPitch;
                for (int j = 0; j < player->frame->width / 8; j++) {
                    uint64_t u = *(uint64_t*)(player->frame->data[1] + u_offset + j * 8);
                    uint64_t v = *(uint64_t*)(player->frame->data[2] + u_offset + j * 8);
                    __m128i data = _mm_set_epi64x(u, v);
                    __m128i ordered = _mm_shuffle_epi8(data, filter);
                    _mm_store_si128((__m128i*)((uint8_t*)map.pData + tex_offset + j * 16), ordered);
                }
            }
            d3d11DeviceContext->Unmap(player->staging_nv12_texture, 0);

            d3d11DeviceContext->PSSetShaderResources(0, 1, &player->staging_luminance_view);
            d3d11DeviceContext->PSSetShaderResources(1, 1, &player->staging_chrominance_view);
            d3d11DeviceContext->Draw(6, 0);
        }
        else if (player->frame->format == AV_PIX_FMT_YUV420P10LE) {
            D3D11_MAPPED_SUBRESOURCE map;
            HRESULT hr = d3d11DeviceContext->Map(player->staging_nv12_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            if (FAILED(hr)) return;

            for (int i = 0; i < player->frame->height; i++) {
                int y_offset = i * player->frame->linesize[0];
                int tex_offset = i * map.RowPitch;
                for (int j = 0; j < player->frame->width / 8; j++) {
                    __m128i data = _mm_load_si128((__m128i*)(player->frame->data[0] + y_offset + j * 16));
                    _mm_store_si128((__m128i*)((uint8_t*)map.pData + tex_offset + j * 16), _mm_slli_epi16(data, 6));
                }
            }

            const __m128i filter = _mm_set_epi8(7, 6, 7 + 8, 6 + 8, 5, 4, 5 + 8, 4 + 8, 3, 2, 3 + 8, 2 + 8, 1, 0, 1 + 8, 0 + 8);
            for (int i = 0; i < player->frame->height / 2; i++) {
                int u_offset = i * player->frame->linesize[1];
                int v_offset = i * player->frame->linesize[2];
                int tex_offset = (i + player->frame->height) * map.RowPitch;
                for (int j = 0; j < player->frame->width / 8; j++) {
                    uint64_t u = *(uint64_t*)(player->frame->data[1] + u_offset + j * 8);
                    uint64_t v = *(uint64_t*)(player->frame->data[2] + u_offset + j * 8);
                    __m128i data = _mm_set_epi64x(u, v);
                    __m128i ordered = _mm_shuffle_epi8(_mm_slli_epi16(data, 6), filter);
                    _mm_store_si128((__m128i*)((uint8_t*)map.pData + tex_offset + j * 16), ordered);
                }
            }
            d3d11DeviceContext->Unmap(player->staging_nv12_texture, 0);

            d3d11DeviceContext->PSSetShaderResources(0, 1, &player->staging_luminance_view);
            d3d11DeviceContext->PSSetShaderResources(1, 1, &player->staging_chrominance_view);
            d3d11DeviceContext->Draw(6, 0);
        }
        player->frame_updated = false;
    }

    // Taken from rediva
    float width = player->decoder_ctx->width;
    float height = player->decoder_ctx->height;
    float scale = movie->params.scale;

    Vec2 sprite_size;
    sprite_size.x = width;
    sprite_size.x = height;

    float tex_x = 0.0;
    float tex_y = 0.0;
    float tex_width;
    float tex_height;

    if (scale <= 0.0) {
        scale = fmin(movie->params.rect.z / width, movie->params.rect.w / height);
        float ar = width / height;
        float desired_ar = movie->params.rect.z / movie->params.rect.w;

        tex_width = (1.0 / scale) * (ar < desired_ar ? movie->params.rect.z / desired_ar * ar : movie->params.rect.z);
        tex_height = (1.0 / scale) * (ar > desired_ar ? movie->params.rect.w / desired_ar * ar : movie->params.rect.w);

        sprite_size.x = tex_width;
        sprite_size.y = tex_height;

        tex_x = width * 0.5 - tex_width * 0.5;
        tex_y = height - height * 0.5 - tex_height * 0.5;
    }
    else {
        tex_width = width;
        tex_height = height;
    }

    SpriteArgs args = { 0 };
    DefaultSpriteArgs(&args);

    args.id = -1;
    args.texture = player->game_texture;
    args.index = movie->params.index;
    args.priority = movie->params.priority;
    args.resolution_mode_screen = movie->params.resolution_mode;
    args.resolution_mode_sprite = movie->params.resolution_mode;
    args.scale.x = scale;
    args.scale.y = scale;
    args.flags = 0x03;
    args.texture_pos.x = tex_x;
    args.texture_pos.y = tex_y;
    args.texture_size.x = tex_width;
    args.texture_size.y = tex_height;
    args.sprite_size = sprite_size;
    args.attr = 0x00420000;
    args.trans.x = movie->params.rect.x + movie->params.rect.z * 0.5;
    args.trans.y = movie->params.rect.y + movie->params.rect.w * 0.5;

    put_sprite(&args);
}

// TODO: Convert RGBA to BT601 FULL UV12
HOOK(void, __fastcall, TaskMovieGetTexture, 0x140455240, TaskMovie* movie, TaskMovie::TextureInfo* texture) {
    if (!movie->is_ffmpeg) return originalTaskMovieGetTexture(movie, texture);

    if (movie->state != TaskMovie::State::Disp || movie->disp_type != TaskMovie::DispType::Texture) return;
    auto player = &ffmpeg_players[movie->index];
    if (player->status != FFmpegPlayer::Status::Playing) return;

    texture->g_texture_y = player->game_texture;
    texture->width = player->decoder_ctx->width;
    texture->height = player->decoder_ctx->height;
}

HOOK(bool, __fastcall, TaskMovieReset, 0x140454B60, TaskMovie* movie, TaskMovie::SprParams* params) {
    movie->is_ffmpeg = false;

    return originalTaskMovieReset(movie, params);
}

HOOK(void, __fastcall, TaskMovieStart, 0x140454CC0, TaskMovie* movie, prj::string* path) {
    prj::string buf("");
    if (*(uint32_t*)(path->c_str() + path->length() - 4) == *(uint32_t*)".usm") {
        movie->is_ffmpeg = false;
        if (ResolveFilePath(path, &buf)) return originalTaskMovieStart(movie, path);
        else {
            DelTask(movie);
            return;
        }
    }

    if (!ResolveFilePath(path, &buf)) {
        *(uint32_t*)(path->c_str() + path->length() - 4) = *(uint32_t*)".usm";
        movie->is_ffmpeg = false;
        if (ResolveFilePath(path, &buf)) return originalTaskMovieStart(movie, path);
        else {
            DelTask(movie);
            return;
        }
    }

    auto player = &ffmpeg_players[movie->index];
    movie->is_ffmpeg = true;
    if (player->input_ctx == nullptr || strcmp(player->input_ctx->url, buf.c_str()) != 0) {
        movie->state = TaskMovie::State::Init;
        movie->path = buf.c_str();
        player->status = FFmpegPlayer::Status::NotInitialized;
    }
    else {
        movie->paused = false;
    }
    implOfTaskMovieCtrlPlayer(movie);
}

HOOK(VkResult, __fastcall, VkCreateDevice, nullptr, VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    vkDeviceExtensions.reserve(pCreateInfo->enabledExtensionCount);

    std::map<const char*, bool> DesiredExtensions = {
        { VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_DECODE_VP9_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_QUEUE_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME, false },
        { VK_KHR_VIDEO_MAINTENANCE_2_EXTENSION_NAME, false },
        { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, false },
        { VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME, false },
        { VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME, false },
        { VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME, false },
        { VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME, false },
        { VK_EXT_SHADER_OBJECT_EXTENSION_NAME, false },
        { VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME, false },
        { VK_KHR_SHADER_EXPECT_ASSUME_EXTENSION_NAME, false },
        { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, false },
        { VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME, false },
        { VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME, false },
        { VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, false },
        { VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME, false },
        { VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, false },
        { VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME, false },
    };

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        vkDeviceExtensions.push_back((char*)calloc(strlen(pCreateInfo->ppEnabledExtensionNames[i]) + 1, sizeof(char)));
        strcpy(vkDeviceExtensions.at(i), pCreateInfo->ppEnabledExtensionNames[i]);

        for (auto& extension : DesiredExtensions) {
            if (strcmp(pCreateInfo->ppEnabledExtensionNames[i], extension.first) == 0) {
                extension.second = true;
                break;
            }
        }
    }

    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, nullptr);
    auto extensions = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * extension_count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, extensions);

    for (uint32_t i = 0; i < extension_count; i++) {
        for (auto& extension : DesiredExtensions) {
            if (!extension.second && strcmp(extensions[i].extensionName, extension.first) == 0) {
                vkDeviceExtensions.push_back((char*)extension.first);
                break;
            }
        }
    }

    free(extensions);

    std::map<VkStructureType, bool> DesiredFeatures = {
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES_KHR, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_2_FEATURES_KHR, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_DECODE_VP9_FEATURES_KHR, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT, false },
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR, false }
    };

    VkPhysicalDeviceFeatures2 enabledFeatures = {};
    VkPhysicalDeviceVulkan11Features vulkan11Features = {};
    VkPhysicalDeviceVulkan12Features vulkan12Features = {};
    VkPhysicalDeviceVulkan13Features vulkan13Features = {};
    VkPhysicalDeviceVulkan14Features vulkan14Features = {};
    VkPhysicalDeviceShaderSubgroupRotateFeatures shaderSubgroupFeatures = {};
    VkPhysicalDeviceHostImageCopyFeaturesEXT hostImageCopyFeatures = {};
    VkPhysicalDeviceShaderExpectAssumeFeaturesKHR shaderExpectAssumeFeatures = {};
    VkPhysicalDeviceVideoMaintenance1FeaturesKHR videoMaintenance1Features = {};
    VkPhysicalDeviceVideoMaintenance2FeaturesKHR videoMaintenance2Features = {};
    VkPhysicalDeviceVideoDecodeVP9FeaturesKHR vp9Features = {};
    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeatures = {};
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperativeMatrixFeatures = {};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures = {};
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT shaderAtomicFloatFeatures = {};
    VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR shaderRelaxFeatures = {};
    
    enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan14Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    shaderSubgroupFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR;
    hostImageCopyFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT;
    shaderExpectAssumeFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES_KHR;
    videoMaintenance1Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR;
    videoMaintenance2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_2_FEATURES_KHR;
    vp9Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_DECODE_VP9_FEATURES_KHR;
    shaderObjectFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
    cooperativeMatrixFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
    descriptorBufferFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    shaderAtomicFloatFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
    shaderRelaxFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR;

    enabledFeatures.pNext = &vulkan11Features;
    vulkan11Features.pNext = &vulkan12Features;
    vulkan12Features.pNext = &vulkan13Features;
    vulkan13Features.pNext = &vulkan14Features;
    vulkan14Features.pNext = &shaderSubgroupFeatures;
    shaderSubgroupFeatures.pNext = &hostImageCopyFeatures;
    hostImageCopyFeatures.pNext = &shaderExpectAssumeFeatures;
    shaderExpectAssumeFeatures.pNext = &videoMaintenance1Features;
    videoMaintenance1Features.pNext = &videoMaintenance2Features;
    videoMaintenance2Features.pNext = &vp9Features;
    vp9Features.pNext = &shaderObjectFeatures;
    shaderObjectFeatures.pNext = &cooperativeMatrixFeatures;
    cooperativeMatrixFeatures.pNext = &descriptorBufferFeatures;
    descriptorBufferFeatures.pNext = &shaderAtomicFloatFeatures;
    shaderAtomicFloatFeatures.pNext = &shaderRelaxFeatures;
    shaderRelaxFeatures.pNext = nullptr;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &enabledFeatures);

    vkDeviceFeatures = {};
    vkDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    if (pCreateInfo->pEnabledFeatures != nullptr) {
        memcpy(&vkDeviceFeatures.features, pCreateInfo->pEnabledFeatures, sizeof(VkPhysicalDeviceFeatures));

        vkDeviceFeatures.features.shaderImageGatherExtended = enabledFeatures.features.shaderImageGatherExtended;
        vkDeviceFeatures.features.shaderStorageImageReadWithoutFormat = enabledFeatures.features.shaderStorageImageReadWithoutFormat;
        vkDeviceFeatures.features.shaderStorageImageWriteWithoutFormat = enabledFeatures.features.shaderStorageImageWriteWithoutFormat;
        vkDeviceFeatures.features.fragmentStoresAndAtomics = enabledFeatures.features.fragmentStoresAndAtomics;
        vkDeviceFeatures.features.vertexPipelineStoresAndAtomics = enabledFeatures.features.vertexPipelineStoresAndAtomics;
        vkDeviceFeatures.features.shaderInt64 = enabledFeatures.features.shaderInt64;
        vkDeviceFeatures.features.shaderInt16 = enabledFeatures.features.shaderInt16;
        vkDeviceFeatures.features.shaderFloat64 = enabledFeatures.features.shaderFloat64;
    }

    const void* pNext = pCreateInfo->pNext;
    void** ppNext = &vkDeviceFeatures.pNext;

    while (pNext != nullptr) {
        auto sType = *(VkStructureType*)pNext;

        if (DesiredFeatures.find(sType) != DesiredFeatures.end()) {
            DesiredFeatures.find(sType)->second = true;
        }

        size_t size = 0;
        switch (sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
            memcpy(&vkDeviceFeatures.features, (void*)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, features)), sizeof(VkPhysicalDeviceFeatures));
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));

            vkDeviceFeatures.features.shaderImageGatherExtended = enabledFeatures.features.shaderImageGatherExtended;
            vkDeviceFeatures.features.shaderStorageImageReadWithoutFormat = enabledFeatures.features.shaderStorageImageReadWithoutFormat;
            vkDeviceFeatures.features.shaderStorageImageWriteWithoutFormat = enabledFeatures.features.shaderStorageImageWriteWithoutFormat;
            vkDeviceFeatures.features.fragmentStoresAndAtomics = enabledFeatures.features.fragmentStoresAndAtomics;
            vkDeviceFeatures.features.vertexPipelineStoresAndAtomics = enabledFeatures.features.vertexPipelineStoresAndAtomics;
            vkDeviceFeatures.features.shaderInt64 = enabledFeatures.features.shaderInt64;
            vkDeviceFeatures.features.shaderInt16 = enabledFeatures.features.shaderInt16;
            vkDeviceFeatures.features.shaderFloat64 = enabledFeatures.features.shaderFloat64;

            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            *ppNext = malloc(sizeof(VkPhysicalDeviceVulkan11Features));
            memcpy(*ppNext, pNext, sizeof(VkPhysicalDeviceVulkan11Features));

            auto features = (VkPhysicalDeviceVulkan11Features*)*ppNext;
            features->samplerYcbcrConversion = vulkan11Features.samplerYcbcrConversion;
            features->storagePushConstant16 = vulkan11Features.storagePushConstant16;
            features->storageBuffer16BitAccess = vulkan11Features.storageBuffer16BitAccess;
            features->uniformAndStorageBuffer16BitAccess = vulkan11Features.uniformAndStorageBuffer16BitAccess;

            ppNext = (void**)((uint64_t)*ppNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));

            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            *ppNext = malloc(sizeof(VkPhysicalDeviceVulkan12Features));
            memcpy(*ppNext, pNext, sizeof(VkPhysicalDeviceVulkan12Features));

            auto features = (VkPhysicalDeviceVulkan12Features*)*ppNext;
            features->timelineSemaphore = vulkan12Features.timelineSemaphore;
            features->scalarBlockLayout = vulkan12Features.scalarBlockLayout;
            features->bufferDeviceAddress = vulkan12Features.bufferDeviceAddress;
            features->hostQueryReset = vulkan12Features.hostQueryReset;
            features->storagePushConstant8 = vulkan12Features.storagePushConstant8;
            features->shaderInt8 = vulkan12Features.shaderInt8;
            features->storageBuffer8BitAccess = vulkan12Features.storageBuffer8BitAccess;
            features->uniformAndStorageBuffer8BitAccess = vulkan12Features.uniformAndStorageBuffer8BitAccess;
            features->shaderFloat16 = vulkan12Features.shaderFloat16;
            features->shaderBufferInt64Atomics = vulkan12Features.shaderBufferInt64Atomics;
            features->shaderSharedInt64Atomics = vulkan12Features.shaderSharedInt64Atomics;
            features->vulkanMemoryModel = vulkan12Features.vulkanMemoryModel;
            features->vulkanMemoryModelDeviceScope = vulkan12Features.vulkanMemoryModelDeviceScope;
            features->uniformBufferStandardLayout = vulkan12Features.uniformBufferStandardLayout;

            ppNext = (void**)((uint64_t)*ppNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));

            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
            *ppNext = malloc(sizeof(VkPhysicalDeviceVulkan13Features));
            memcpy(*ppNext, pNext, sizeof(VkPhysicalDeviceVulkan13Features));

            auto features = (VkPhysicalDeviceVulkan13Features*)*ppNext;
            features->dynamicRendering = vulkan13Features.dynamicRendering;
            features->maintenance4 = vulkan13Features.maintenance4;
            features->synchronization2 = vulkan13Features.synchronization2;
            features->computeFullSubgroups = vulkan13Features.computeFullSubgroups;
            features->subgroupSizeControl = vulkan13Features.subgroupSizeControl;
            features->shaderZeroInitializeWorkgroupMemory = vulkan13Features.shaderZeroInitializeWorkgroupMemory;

            ppNext = (void**)((uint64_t)*ppNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));

            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES: {
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: {
            *ppNext = malloc(sizeof(VkPhysicalDeviceVulkan14Features));
            memcpy(*ppNext, pNext, sizeof(VkPhysicalDeviceVulkan14Features));

            auto features = (VkPhysicalDeviceVulkan14Features*)*ppNext;
            features->shaderSubgroupRotate = vulkan14Features.shaderSubgroupRotate;
            features->hostImageCopy = vulkan14Features.hostImageCopy;
            features->shaderExpectAssume = vulkan14Features.shaderExpectAssume;

            ppNext = (void**)((uint64_t)*ppNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
            pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));

            continue;
        };
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceShaderSubgroupRotateFeatures);
            auto next = (VkPhysicalDeviceShaderSubgroupRotateFeatures*)pNext;
            next->shaderSubgroupRotate = shaderSubgroupFeatures.shaderSubgroupRotate;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT: {
            size = sizeof(VkPhysicalDeviceHostImageCopyFeatures);
            auto next = (VkPhysicalDeviceHostImageCopyFeatures*)pNext;
            next->hostImageCopy = hostImageCopyFeatures.hostImageCopy;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceShaderExpectAssumeFeatures);
            auto next = (VkPhysicalDeviceShaderExpectAssumeFeatures*)pNext;
            next->shaderExpectAssume = shaderExpectAssumeFeatures.shaderExpectAssume;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceVideoMaintenance1FeaturesKHR);
            auto next = (VkPhysicalDeviceVideoMaintenance1FeaturesKHR*)pNext;
            next->videoMaintenance1 = videoMaintenance1Features.videoMaintenance1;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_2_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceVideoMaintenance2FeaturesKHR);
            auto next = (VkPhysicalDeviceVideoMaintenance2FeaturesKHR*)pNext;
            next->videoMaintenance2 = videoMaintenance2Features.videoMaintenance2;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_DECODE_VP9_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceVideoDecodeVP9FeaturesKHR);
            auto next = (VkPhysicalDeviceVideoDecodeVP9FeaturesKHR*)pNext;
            next->videoDecodeVP9 = vp9Features.videoDecodeVP9;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT: {
            size = sizeof(VkPhysicalDeviceShaderObjectFeaturesEXT);
            auto next = (VkPhysicalDeviceShaderObjectFeaturesEXT*)pNext;
            next->shaderObject = shaderObjectFeatures.shaderObject;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceCooperativeMatrixFeaturesKHR);
            auto next = (VkPhysicalDeviceCooperativeMatrixFeaturesKHR*)pNext;
            next->cooperativeMatrix = cooperativeMatrixFeatures.cooperativeMatrix;
            next->cooperativeMatrixRobustBufferAccess = cooperativeMatrixFeatures.cooperativeMatrixRobustBufferAccess;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT: {
            size = sizeof(VkPhysicalDeviceDescriptorBufferFeaturesEXT);
            auto next = (VkPhysicalDeviceDescriptorBufferFeaturesEXT*)pNext;
            next->descriptorBuffer = descriptorBufferFeatures.descriptorBuffer;
            next->descriptorBufferPushDescriptors = descriptorBufferFeatures.descriptorBufferPushDescriptors;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: {
            size = sizeof(VkPhysicalDeviceShaderAtomicFloatFeaturesEXT);
            auto next = (VkPhysicalDeviceShaderAtomicFloatFeaturesEXT*)pNext;
            next->shaderBufferFloat32Atomics = shaderAtomicFloatFeatures.shaderBufferFloat32Atomics;
            next->shaderBufferFloat32AtomicAdd = shaderAtomicFloatFeatures.shaderBufferFloat32AtomicAdd;
        }; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR: {
            size = sizeof(VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR);
            auto next = (VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR*)pNext;
            next->shaderRelaxedExtendedInstruction = shaderRelaxFeatures.shaderRelaxedExtendedInstruction;
        }; break;
        case VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT: size = sizeof(VkDeviceDeviceMemoryReportCreateInfoEXT); break;
        case VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV: size = sizeof(VkDeviceDiagnosticsConfigCreateInfoNV); break;
        case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO: size = sizeof(VkDeviceGroupDeviceCreateInfo); break;
        case VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD: size = sizeof(VkDeviceMemoryOverallocationCreateInfoAMD); break;
        case VK_STRUCTURE_TYPE_DEVICE_PIPELINE_BINARY_INTERNAL_CACHE_CONTROL_KHR: size = sizeof(VkDevicePipelineBinaryInternalCacheControlKHR); break;
        case VK_STRUCTURE_TYPE_DEVICE_PRIVATE_DATA_CREATE_INFO: size = sizeof(VkDevicePrivateDataCreateInfo); break;
        case VK_STRUCTURE_TYPE_DEVICE_QUEUE_SHADER_CORE_CONTROL_CREATE_INFO_ARM: size = sizeof(VkDeviceQueueShaderCoreControlCreateInfoARM); break;
        case VK_STRUCTURE_TYPE_EXTERNAL_COMPUTE_QUEUE_DEVICE_CREATE_INFO_NV: size = sizeof(VkExternalComputeQueueDeviceCreateInfoNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: size = sizeof(VkPhysicalDevice4444FormatsFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ASTC_DECODE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceASTCDecodeFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: size = sizeof(VkPhysicalDeviceAccelerationStructureFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT: size = sizeof(VkPhysicalDeviceAddressBindingReportFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_AMIGO_PROFILING_FEATURES_SEC: size = sizeof(VkPhysicalDeviceAmigoProfilingFeaturesSEC); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD: size = sizeof(VkPhysicalDeviceAntiLagFeaturesAMD); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT: size = sizeof(VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT: size = sizeof(VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceBorderColorSwizzleFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceBufferDeviceAddressFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV: size = sizeof(VkPhysicalDeviceClusterAccelerationStructureFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_CULLING_SHADER_FEATURES_HUAWEI: size = sizeof(VkPhysicalDeviceClusterCullingShaderFeaturesHUAWEI); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD: size = sizeof(VkPhysicalDeviceCoherentMemoryFeaturesAMD); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceColorWriteEnableFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMMAND_BUFFER_INHERITANCE_FEATURES_NV: size = sizeof(VkPhysicalDeviceCommandBufferInheritanceFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR: size = sizeof(VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: size = sizeof(VkPhysicalDeviceConditionalRenderingFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV: size = sizeof(VkPhysicalDeviceCooperativeMatrix2FeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_NV: size = sizeof(VkPhysicalDeviceCooperativeMatrixFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV: size = sizeof(VkPhysicalDeviceCooperativeVectorFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COPY_MEMORY_INDIRECT_FEATURES_NV: size = sizeof(VkPhysicalDeviceCopyMemoryIndirectFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CORNER_SAMPLED_IMAGE_FEATURES_NV: size = sizeof(VkPhysicalDeviceCornerSampledImageFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COVERAGE_REDUCTION_MODE_FEATURES_NV: size = sizeof(VkPhysicalDeviceCoverageReductionModeFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_CLAMP_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceCubicClampFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUBIC_WEIGHTS_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceCubicWeightsFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: size = sizeof(VkPhysicalDeviceCustomBorderColorFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DATA_GRAPH_FEATURES_ARM: size = sizeof(VkPhysicalDeviceDataGraphFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEDICATED_ALLOCATION_IMAGE_ALIASING_FEATURES_NV: size = sizeof(VkPhysicalDeviceDedicatedAllocationImageAliasingFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDepthBiasControlFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_CONTROL_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDepthClampControlFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLAMP_ZERO_ONE_FEATURES_KHR: size = sizeof(VkPhysicalDeviceDepthClampZeroOneFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDepthClipControlFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDepthClipEnableFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_TENSOR_FEATURES_ARM: size = sizeof(VkPhysicalDeviceDescriptorBufferTensorFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: size = sizeof(VkPhysicalDeviceDescriptorIndexingFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_POOL_OVERALLOCATION_FEATURES_NV: size = sizeof(VkPhysicalDeviceDescriptorPoolOverallocationFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE: size = sizeof(VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_COMPUTE_FEATURES_NV: size = sizeof(VkPhysicalDeviceDeviceGeneratedCommandsComputeFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDeviceGeneratedCommandsFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV: size = sizeof(VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_MEMORY_REPORT_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDeviceMemoryReportFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV: size = sizeof(VkPhysicalDeviceDiagnosticsConfigFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES: size = sizeof(VkPhysicalDeviceDynamicRenderingLocalReadFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXCLUSIVE_SCISSOR_FEATURES_NV: size = sizeof(VkPhysicalDeviceExclusiveScissorFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT: size = sizeof(VkPhysicalDeviceExtendedDynamicState2FeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT: size = sizeof(VkPhysicalDeviceExtendedDynamicState3FeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceExtendedDynamicStateFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_SPARSE_ADDRESS_SPACE_FEATURES_NV: size = sizeof(VkPhysicalDeviceExtendedSparseAddressSpaceFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_RDMA_FEATURES_NV: size = sizeof(VkPhysicalDeviceExternalMemoryRDMAFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT: size = sizeof(VkPhysicalDeviceFaultFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FORMAT_PACK_FEATURES_ARM: size = sizeof(VkPhysicalDeviceFormatPackFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_2_FEATURES_EXT: size = sizeof(VkPhysicalDeviceFragmentDensityMap2FeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT: size = sizeof(VkPhysicalDeviceFragmentDensityMapFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_LAYERED_FEATURES_VALVE: size = sizeof(VkPhysicalDeviceFragmentDensityMapLayeredFeaturesVALVE); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_OFFSET_FEATURES_EXT: size = sizeof(VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR: size = sizeof(VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT: size = sizeof(VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_ENUMS_FEATURES_NV: size = sizeof(VkPhysicalDeviceFragmentShadingRateEnumsFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: size = sizeof(VkPhysicalDeviceFragmentShadingRateFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT: size = sizeof(VkPhysicalDeviceFrameBoundaryFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES: size = sizeof(VkPhysicalDeviceGlobalPriorityQueryFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT: size = sizeof(VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HDR_VIVID_FEATURES_HUAWEI: size = sizeof(VkPhysicalDeviceHdrVividFeaturesHUAWEI); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT: size = sizeof(VkPhysicalDeviceImage2DViewOf3DFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_FEATURES_MESA: size = sizeof(VkPhysicalDeviceImageAlignmentControlFeaturesMESA); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT: size = sizeof(VkPhysicalDeviceImageCompressionControlFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_SWAPCHAIN_FEATURES_EXT: size = sizeof(VkPhysicalDeviceImageCompressionControlSwapchainFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_2_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceImageProcessing2FeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_PROCESSING_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceImageProcessingFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES: size = sizeof(VkPhysicalDeviceImageRobustnessFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT: size = sizeof(VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT: size = sizeof(VkPhysicalDeviceImageViewMinLodFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: size = sizeof(VkPhysicalDeviceImagelessFramebufferFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES: size = sizeof(VkPhysicalDeviceIndexTypeUint8Features); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INHERITED_VIEWPORT_SCISSOR_FEATURES_NV: size = sizeof(VkPhysicalDeviceInheritedViewportScissorFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES: size = sizeof(VkPhysicalDeviceInlineUniformBlockFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INVOCATION_MASK_FEATURES_HUAWEI: size = sizeof(VkPhysicalDeviceInvocationMaskFeaturesHUAWEI); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_DITHERING_FEATURES_EXT: size = sizeof(VkPhysicalDeviceLegacyDitheringFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LEGACY_VERTEX_ATTRIBUTES_FEATURES_EXT: size = sizeof(VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES: size = sizeof(VkPhysicalDeviceLineRasterizationFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINEAR_COLOR_ATTACHMENT_FEATURES_NV: size = sizeof(VkPhysicalDeviceLinearColorAttachmentFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES: size = sizeof(VkPhysicalDeviceMaintenance5Features); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES: size = sizeof(VkPhysicalDeviceMaintenance6Features); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR: size = sizeof(VkPhysicalDeviceMaintenance7FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR: size = sizeof(VkPhysicalDeviceMaintenance8FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR: size = sizeof(VkPhysicalDeviceMaintenance9FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT: size = sizeof(VkPhysicalDeviceMapMemoryPlacedFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_NV: size = sizeof(VkPhysicalDeviceMemoryDecompressionFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT: size = sizeof(VkPhysicalDeviceMemoryPriorityFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: size = sizeof(VkPhysicalDeviceMeshShaderFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV: size = sizeof(VkPhysicalDeviceMeshShaderFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT: size = sizeof(VkPhysicalDeviceMultiDrawFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT: size = sizeof(VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: size = sizeof(VkPhysicalDeviceMultiviewFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_RENDER_AREAS_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceMultiviewPerViewRenderAreasFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PER_VIEW_VIEWPORTS_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceMultiviewPerViewViewportsFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NESTED_COMMAND_BUFFER_FEATURES_EXT: size = sizeof(VkPhysicalDeviceNestedCommandBufferFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT: size = sizeof(VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT: size = sizeof(VkPhysicalDeviceOpacityMicromapFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV: size = sizeof(VkPhysicalDeviceOpticalFlowFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT: size = sizeof(VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PARTITIONED_ACCELERATION_STRUCTURE_FEATURES_NV: size = sizeof(VkPhysicalDevicePartitionedAccelerationStructureFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PER_STAGE_DESCRIPTOR_SET_FEATURES_NV: size = sizeof(VkPhysicalDevicePerStageDescriptorSetFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: size = sizeof(VkPhysicalDevicePerformanceQueryFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_BINARY_FEATURES_KHR: size = sizeof(VkPhysicalDevicePipelineBinaryFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CACHE_INCREMENTAL_MODE_FEATURES_SEC: size = sizeof(VkPhysicalDevicePipelineCacheIncrementalModeFeaturesSEC); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES: size = sizeof(VkPhysicalDevicePipelineCreationCacheControlFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: size = sizeof(VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_EXT: size = sizeof(VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_OPACITY_MICROMAP_FEATURES_ARM: size = sizeof(VkPhysicalDevicePipelineOpacityMicromapFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROPERTIES_FEATURES_EXT: size = sizeof(VkPhysicalDevicePipelinePropertiesFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_PROTECTED_ACCESS_FEATURES: size = sizeof(VkPhysicalDevicePipelineProtectedAccessFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES: size = sizeof(VkPhysicalDevicePipelineRobustnessFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_BARRIER_FEATURES_NV: size = sizeof(VkPhysicalDevicePresentBarrierFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR: size = sizeof(VkPhysicalDevicePresentId2FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR: size = sizeof(VkPhysicalDevicePresentIdFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR: size = sizeof(VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR: size = sizeof(VkPhysicalDevicePresentWait2FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR: size = sizeof(VkPhysicalDevicePresentWaitFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: size = sizeof(VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT: size = sizeof(VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES: size = sizeof(VkPhysicalDevicePrivateDataFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: size = sizeof(VkPhysicalDeviceProtectedMemoryFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: size = sizeof(VkPhysicalDeviceProvokingVertexFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RGBA10X6_FORMATS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceRGBA10X6FormatsFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAW_ACCESS_CHAINS_FEATURES_NV: size = sizeof(VkPhysicalDeviceRawAccessChainsFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR: size = sizeof(VkPhysicalDeviceRayQueryFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV: size = sizeof(VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV: size = sizeof(VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR: size = sizeof(VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MOTION_BLUR_FEATURES_NV: size = sizeof(VkPhysicalDeviceRayTracingMotionBlurFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR: size = sizeof(VkPhysicalDeviceRayTracingPipelineFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR: size = sizeof(VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV: size = sizeof(VkPhysicalDeviceRayTracingValidationFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RELAXED_LINE_RASTERIZATION_FEATURES_IMG: size = sizeof(VkPhysicalDeviceRelaxedLineRasterizationFeaturesIMG); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_FEATURES_ARM: size = sizeof(VkPhysicalDeviceRenderPassStripedFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_REPRESENTATIVE_FRAGMENT_TEST_FEATURES_NV: size = sizeof(VkPhysicalDeviceRepresentativeFragmentTestFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR: size = sizeof(VkPhysicalDeviceRobustness2FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCHEDULING_CONTROLS_FEATURES_ARM: size = sizeof(VkPhysicalDeviceSchedulingControlsFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES: size = sizeof(VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT16_VECTOR_FEATURES_NV: size = sizeof(VkPhysicalDeviceShaderAtomicFloat16VectorFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT: size = sizeof(VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_BFLOAT16_FEATURES_KHR: size = sizeof(VkPhysicalDeviceShaderBfloat16FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: size = sizeof(VkPhysicalDeviceShaderClockFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_FEATURES_ARM: size = sizeof(VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES: size = sizeof(VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: size = sizeof(VkPhysicalDeviceShaderDrawParametersFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EARLY_AND_LATE_FRAGMENT_TESTS_FEATURES_AMD: size = sizeof(VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT: size = sizeof(VkPhysicalDeviceShaderFloat8FeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES: size = sizeof(VkPhysicalDeviceShaderFloatControls2Features); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT: size = sizeof(VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_FOOTPRINT_FEATURES_NV: size = sizeof(VkPhysicalDeviceShaderImageFootprintFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES: size = sizeof(VkPhysicalDeviceShaderIntegerDotProductFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL: size = sizeof(VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR: size = sizeof(VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT: size = sizeof(VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR: size = sizeof(VkPhysicalDeviceShaderQuadControlFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_REPLICATED_COMPOSITES_FEATURES_EXT: size = sizeof(VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_FEATURES_NV: size = sizeof(VkPhysicalDeviceShaderSMBuiltinsFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: size = sizeof(VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR: size = sizeof(VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES: size = sizeof(VkPhysicalDeviceShaderTerminateInvocationFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TILE_IMAGE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceShaderTileImageFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR: size = sizeof(VkPhysicalDeviceShaderUntypedPointersFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV: size = sizeof(VkPhysicalDeviceShadingRateImageFeaturesNV); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_MERGE_FEEDBACK_FEATURES_EXT: size = sizeof(VkPhysicalDeviceSubpassMergeFeedbackFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBPASS_SHADING_FEATURES_HUAWEI: size = sizeof(VkPhysicalDeviceSubpassShadingFeaturesHUAWEI); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR: size = sizeof(VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM: size = sizeof(VkPhysicalDeviceTensorFeaturesARM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: size = sizeof(VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES: size = sizeof(VkPhysicalDeviceTextureCompressionASTCHDRFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TILE_MEMORY_HEAP_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceTileMemoryHeapFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TILE_PROPERTIES_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceTilePropertiesFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TILE_SHADING_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceTileShadingFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: size = sizeof(VkPhysicalDeviceTransformFeedbackFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR: size = sizeof(VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: size = sizeof(VkPhysicalDeviceVariablePointersFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES: size = sizeof(VkPhysicalDeviceVertexAttributeDivisorFeatures); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_ROBUSTNESS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: size = sizeof(VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_AV1_FEATURES_KHR: size = sizeof(VkPhysicalDeviceVideoEncodeAV1FeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_INTRA_REFRESH_FEATURES_KHR: size = sizeof(VkPhysicalDeviceVideoEncodeIntraRefreshFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_ENCODE_QUANTIZATION_MAP_FEATURES_KHR: size = sizeof(VkPhysicalDeviceVideoEncodeQuantizationMapFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: size = sizeof(VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_DEGAMMA_FEATURES_QCOM: size = sizeof(VkPhysicalDeviceYcbcrDegammaFeaturesQCOM); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: size = sizeof(VkPhysicalDeviceYcbcrImageArraysFeaturesEXT); break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_DEVICE_MEMORY_FEATURES_EXT: size = sizeof(VkPhysicalDeviceZeroInitializeDeviceMemoryFeaturesEXT); break;
        };

        *ppNext = malloc(size);
        memcpy(*ppNext, pNext, size);
        ppNext = (void **)((uint64_t)*ppNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
        pNext = *(const void**)((uint64_t)pNext + offsetof(VkPhysicalDeviceFeatures2, pNext));
    }

    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES] == false) {
        auto next = (VkPhysicalDeviceVulkan11Features*)malloc(sizeof(VkPhysicalDeviceVulkan11Features));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        next->samplerYcbcrConversion = vulkan11Features.samplerYcbcrConversion;
        next->storagePushConstant16 = vulkan11Features.storagePushConstant16;
        next->storageBuffer16BitAccess = vulkan11Features.storageBuffer16BitAccess;
        next->uniformAndStorageBuffer16BitAccess = vulkan11Features.uniformAndStorageBuffer16BitAccess;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES] == false) {
        auto next = (VkPhysicalDeviceVulkan12Features*)malloc(sizeof(VkPhysicalDeviceVulkan12Features));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        next->timelineSemaphore = vulkan12Features.timelineSemaphore;
        next->scalarBlockLayout = vulkan12Features.scalarBlockLayout;
        next->bufferDeviceAddress = vulkan12Features.bufferDeviceAddress;
        next->hostQueryReset = vulkan12Features.hostQueryReset;
        next->storagePushConstant8 = vulkan12Features.storagePushConstant8;
        next->shaderInt8 = vulkan12Features.shaderInt8;
        next->storageBuffer8BitAccess = vulkan12Features.storageBuffer8BitAccess;
        next->uniformAndStorageBuffer8BitAccess = vulkan12Features.uniformAndStorageBuffer8BitAccess;
        next->shaderFloat16 = vulkan12Features.shaderFloat16;
        next->shaderBufferInt64Atomics = vulkan12Features.shaderBufferInt64Atomics;
        next->shaderSharedInt64Atomics = vulkan12Features.shaderSharedInt64Atomics;
        next->vulkanMemoryModel = vulkan12Features.vulkanMemoryModel;
        next->vulkanMemoryModelDeviceScope = vulkan12Features.vulkanMemoryModelDeviceScope;
        next->uniformBufferStandardLayout = vulkan12Features.uniformBufferStandardLayout;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES] == false) {
        auto next = (VkPhysicalDeviceVulkan13Features*)malloc(sizeof(VkPhysicalDeviceVulkan13Features));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        next->dynamicRendering = vulkan13Features.dynamicRendering;
        next->maintenance4 = vulkan13Features.maintenance4;
        next->synchronization2 = vulkan13Features.synchronization2;
        next->computeFullSubgroups = vulkan13Features.computeFullSubgroups;
        next->subgroupSizeControl = vulkan13Features.subgroupSizeControl;
        next->shaderZeroInitializeWorkgroupMemory = vulkan13Features.shaderZeroInitializeWorkgroupMemory;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR] == false && DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES] == false) {
        auto next = (VkPhysicalDeviceShaderSubgroupRotateFeaturesKHR*)malloc(sizeof(VkPhysicalDeviceShaderSubgroupRotateFeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_ROTATE_FEATURES_KHR;
        next->shaderSubgroupRotate = shaderSubgroupFeatures.shaderSubgroupRotate;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT] == false && DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES] == false) {
        auto next = (VkPhysicalDeviceHostImageCopyFeaturesEXT*)malloc(sizeof(VkPhysicalDeviceHostImageCopyFeaturesEXT));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT;
        next->hostImageCopy = hostImageCopyFeatures.hostImageCopy;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES_KHR] == false && DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES] == false) {
        auto next = (VkPhysicalDeviceShaderExpectAssumeFeaturesKHR*)malloc(sizeof(VkPhysicalDeviceShaderExpectAssumeFeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EXPECT_ASSUME_FEATURES_KHR;
        next->shaderExpectAssume = shaderExpectAssumeFeatures.shaderExpectAssume;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR] == false) {
        auto next = (VkPhysicalDeviceVideoMaintenance1FeaturesKHR*)malloc(sizeof(VkPhysicalDeviceVideoMaintenance1FeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_1_FEATURES_KHR;
        next->videoMaintenance1 = videoMaintenance1Features.videoMaintenance1;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_2_FEATURES_KHR] == false) {
        auto next = (VkPhysicalDeviceVideoMaintenance2FeaturesKHR*)malloc(sizeof(VkPhysicalDeviceVideoMaintenance2FeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_MAINTENANCE_2_FEATURES_KHR;
        next->videoMaintenance2 = videoMaintenance2Features.videoMaintenance2;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_DECODE_VP9_FEATURES_KHR] == false) {
        auto next = (VkPhysicalDeviceVideoDecodeVP9FeaturesKHR*)malloc(sizeof(VkPhysicalDeviceVideoDecodeVP9FeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_DECODE_VP9_FEATURES_KHR;
        next->videoDecodeVP9 = vp9Features.videoDecodeVP9;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT] == false) {
        auto next = (VkPhysicalDeviceShaderObjectFeaturesEXT*)malloc(sizeof(VkPhysicalDeviceShaderObjectFeaturesEXT));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
        next->shaderObject = shaderObjectFeatures.shaderObject;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR] == false) {
        auto next = (VkPhysicalDeviceCooperativeMatrixFeaturesKHR*)malloc(sizeof(VkPhysicalDeviceCooperativeMatrixFeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
        next->cooperativeMatrix = cooperativeMatrixFeatures.cooperativeMatrix;
        next->cooperativeMatrixRobustBufferAccess = cooperativeMatrixFeatures.cooperativeMatrixRobustBufferAccess;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT] == false) {
        auto next = (VkPhysicalDeviceDescriptorBufferFeaturesEXT*)malloc(sizeof(VkPhysicalDeviceDescriptorBufferFeaturesEXT));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
        next->descriptorBuffer = descriptorBufferFeatures.descriptorBuffer;
        next->descriptorBufferPushDescriptors = descriptorBufferFeatures.descriptorBufferPushDescriptors;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT] == false) {
        auto next = (VkPhysicalDeviceShaderAtomicFloatFeaturesEXT*)malloc(sizeof(VkPhysicalDeviceShaderAtomicFloatFeaturesEXT));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
        next->shaderBufferFloat32Atomics = shaderAtomicFloatFeatures.shaderBufferFloat32Atomics;
        next->shaderBufferFloat32AtomicAdd = shaderAtomicFloatFeatures.shaderBufferFloat32AtomicAdd;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    if (DesiredFeatures[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR] == false) {
        auto next = (VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR*)malloc(sizeof(VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR));
        *next = {};
        next->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR;
        next->shaderRelaxedExtendedInstruction = shaderRelaxFeatures.shaderRelaxedExtendedInstruction;
        *ppNext = next;
        ppNext = &next->pNext;
    }
    *ppNext = nullptr;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queue_family_count, nullptr);

    VkQueueFamilyProperties2* queue_familes = (VkQueueFamilyProperties2*)malloc(sizeof(VkQueueFamilyProperties2) * queue_family_count);
    VkQueueFamilyVideoPropertiesKHR* queue_video_props = (VkQueueFamilyVideoPropertiesKHR*)malloc(sizeof(VkQueueFamilyVideoPropertiesKHR) * queue_family_count);
    for (uint32_t i = 0; i < queue_family_count; i++) {
        queue_familes[i] = {};
        queue_familes[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        queue_familes[i].pNext = &queue_video_props[i];
        queue_video_props[i] = {};
        queue_video_props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
        queue_video_props[i].pNext = nullptr;
    }

    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queue_family_count, queue_familes);

    uint32_t graphics_queue_family = -1;
    uint32_t graphics_queue_family_flag_count = -1;
    uint32_t compute_queue_family = -1;
    uint32_t compute_queue_family_flag_count = -1;
    uint32_t transfer_queue_family = -1;
    uint32_t transfer_queue_family_flag_count = -1;
    uint32_t video_decode_queue_family = -1;
    uint32_t video_decode_queue_family_flag_count = -1;

    for (uint32_t i = 0; i < queue_family_count; i++) {
        uint32_t flag_count = 0;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT || queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT || queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_PROTECTED_BIT) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) flag_count++;
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_DATA_GRAPH_BIT_ARM) flag_count++;

        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && flag_count < graphics_queue_family_flag_count) {
            graphics_queue_family = i;
            graphics_queue_family_flag_count = flag_count;
        }
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT && flag_count < compute_queue_family_flag_count) {
            compute_queue_family = i;
            graphics_queue_family_flag_count = flag_count;
        }
        if ((queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT || queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT || queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) && flag_count < transfer_queue_family_flag_count) {
            transfer_queue_family = i;
            transfer_queue_family_flag_count = flag_count;
        }
        if (queue_familes[i].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR && flag_count < video_decode_queue_family_flag_count) {
            video_decode_queue_family = i;
            video_decode_queue_family_flag_count = flag_count;
        }
    }

    std::set<uint32_t> unique_queues = { graphics_queue_family, compute_queue_family, transfer_queue_family, video_decode_queue_family };

    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
        unique_queues.insert(pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex);
    }

    auto queueCreateInfos = (VkDeviceQueueCreateInfo*)malloc(sizeof(VkDeviceQueueCreateInfo) * unique_queues.size());
    vkQueueFamilies.reserve(unique_queues.size());
    uint32_t i = 0;
    for (auto& queue : unique_queues) {
        queueCreateInfos[i] = {};
        queueCreateInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfos[i].pNext = nullptr;
        queueCreateInfos[i].queueFamilyIndex = queue;
        queueCreateInfos[i].queueCount = queue_familes[queue].queueFamilyProperties.queueCount;

        auto priorities = (float*)malloc(sizeof(float) * queue_familes[queue].queueFamilyProperties.queueCount);
        for (uint32_t j = 0; j < queue_familes[queue].queueFamilyProperties.queueCount; j++) {
            priorities[j] = 1.0;
        }
        queueCreateInfos[i].pQueuePriorities = priorities;

        AVVulkanDeviceQueueFamily qf = {};
        qf.idx = queue;
        qf.num = queue_familes[queue].queueFamilyProperties.queueCount;
        qf.flags = (VkQueueFlagBits)queue_familes[queue].queueFamilyProperties.queueFlags;
        qf.video_caps = (VkVideoCodecOperationFlagBitsKHR)queue_video_props[queue].videoCodecOperations;
        vkQueueFamilies.push_back(qf);

        i++;
    }

    free(queue_familes);
    free(queue_video_props);
    
    const char* const* ppEnabledExtensionNames = pCreateInfo->ppEnabledExtensionNames;
    uint32_t enabledExtensionCount = pCreateInfo->enabledExtensionCount;
    pNext = pCreateInfo->pNext;
    const VkPhysicalDeviceFeatures* pEnabledFeatures = pCreateInfo->pEnabledFeatures;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos = pCreateInfo->pQueueCreateInfos;
    uint32_t queueCreateInfoCount = pCreateInfo->queueCreateInfoCount;
    pCreateInfo->ppEnabledExtensionNames = vkDeviceExtensions.data();
    pCreateInfo->enabledExtensionCount = vkDeviceExtensions.size();
    pCreateInfo->pNext = &vkDeviceFeatures;
    pCreateInfo->pEnabledFeatures = nullptr;
    pCreateInfo->pQueueCreateInfos = queueCreateInfos;
    pCreateInfo->queueCreateInfoCount = unique_queues.size();

    VkResult res = originalVkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    pCreateInfo->ppEnabledExtensionNames = ppEnabledExtensionNames;
    pCreateInfo->enabledExtensionCount = enabledExtensionCount;
    pCreateInfo->pNext = pNext;
    pCreateInfo->pEnabledFeatures = pEnabledFeatures;
    pCreateInfo->pQueueCreateInfos = pQueueCreateInfos;
    pCreateInfo->queueCreateInfoCount = queueCreateInfoCount;

    for (uint32_t i = 0; i < unique_queues.size(); i++) {
        free((float*)queueCreateInfos[i].pQueuePriorities);
    }
    free(queueCreateInfos);

    if (res == VK_SUCCESS) {
        volkLoadDevice(vkDevice);
    }
    return res;
}

HOOK(VkResult, __fastcall, VkCreateInstance, nullptr, VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    vkInstanceExtensions.reserve(pCreateInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        vkInstanceExtensions.push_back((char*)calloc(strlen(pCreateInfo->ppEnabledExtensionNames[i]) + 1, sizeof(char)));
        strcpy(vkInstanceExtensions.at(i), pCreateInfo->ppEnabledExtensionNames[i]);
    }
    vkVersion = pCreateInfo->pApplicationInfo->apiVersion;

    VkResult res = originalVkCreateInstance(pCreateInfo, pAllocator, pInstance);

    if (res == VK_SUCCESS && VK_VERSION_MAJOR(vkVersion) >= 1 && VK_VERSION_MINOR(vkVersion) >= 3) {
        volkLoadInstance(*pInstance);
        originalVkCreateDevice = (VkCreateDeviceDelegate*)vkCreateDevice;
        INSTALL_HOOK(VkCreateDevice);
    }
    return res;
}

void MoviePlayer::preInit() {
    INSTALL_HOOK(TaskMovieInit);
}

void MoviePlayer::init() {
    INSTALL_HOOK(TaskMovieCheckDisp);
    INSTALL_HOOK(TaskMovieCtrl);
    INSTALL_HOOK(TaskMovieCtrlPlayer);
    INSTALL_HOOK(TaskMovieDisp);
    INSTALL_HOOK(TaskMovieGetTexture);
    INSTALL_HOOK(TaskMovieReset);
    INSTALL_HOOK(TaskMovieStart);

    // Remove ResolveFilePath before TaskMovie::Start
    WRITE_NOP(0x14025AF89, 9);
    WRITE_NOP(0x14024ADF0, 9);
    // Skip replacing extension with .usm
    WRITE_MEMORY(0x14025AEC7, uint8_t, 0x90, 0xE9);

    HMODULE dllHandle = GetModuleHandle("ntdll.dll");
    if (dllHandle != nullptr && GetProcAddress(dllHandle, "wine_get_version") != nullptr) {
        VkResult res = volkInitialize();
        if (res != VK_SUCCESS) {
            MessageBoxA(nullptr, "Failed to init vulkan", "MoviePlayer::d3dInit", MB_OK | MB_ICONERROR);
            return;
        }

        vulkan = true;
        originalVkCreateInstance = (VkCreateInstanceDelegate*)vkCreateInstance;
        INSTALL_HOOK(VkCreateInstance);
    }
}

void MoviePlayer::d3dInit(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    d3d11SwapChain = swapChain;
    d3d11Device = device;
    d3d11DeviceContext = deviceContext;

    device->CreateVertexShader(vs_bytecode, sizeof(vs_bytecode), nullptr, &shader_vs);
    device->CreatePixelShader(bt601_full_bytecode, sizeof(bt601_full_bytecode), nullptr, &shader_bt601_full);
    device->CreatePixelShader(bt601_limited_bytecode, sizeof(bt601_limited_bytecode), nullptr, &shader_bt601_limited);
    device->CreatePixelShader(bt709_full_bytecode, sizeof(bt709_full_bytecode), nullptr, &shader_bt709_full);
    device->CreatePixelShader(bt709_limited_bytecode, sizeof(bt709_limited_bytecode), nullptr, &shader_bt709_limited);
    device->CreatePixelShader(bt2020_full_bytecode, sizeof(bt2020_full_bytecode), nullptr, &shader_bt2020_full);
    device->CreatePixelShader(bt2020_limited_bytecode, sizeof(bt2020_limited_bytecode), nullptr, &shader_bt2020_limited);

    IDXGIVkInteropDevice1* vkInteropDevice = nullptr;
    d3d11Device->QueryInterface(&vkInteropDevice);
    if (vulkan && vkInteropDevice != nullptr && VK_VERSION_MAJOR(vkVersion) >= 1 && VK_VERSION_MINOR(vkVersion) >= 3) {
        vkInteropDevice->GetVulkanHandles(&vkInstance, &vkPhysDevice, &vkDevice);
    }
    else {
        vulkan = false;
    }
}