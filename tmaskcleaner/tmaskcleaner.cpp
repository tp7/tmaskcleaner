#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#pragma warning(disable: 4512 4244 4100)
#include "avisynth.h"
#pragma warning(default: 4512 4244 4100)
#include <stdint.h>

typedef std::pair<int, int> Coordinates;

class TMaskCleaner : public GenericVideoFilter {
public:
    TMaskCleaner(PClip child, int length, int thresh, int fade, IScriptEnvironment*);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

    ~TMaskCleaner() {
        delete[] lookup_;
    }
private:
    unsigned int length_;
    unsigned int thresh_;
    unsigned int fade_;
    uint8_t *lookup_;
    int width_;

    void clear_mask(uint8_t *dst, const uint8_t *src, int width, int height, int src_pitch, int dst_pitch);
    void process_pixel(const uint8_t *src, int x, int y, int pitch, int w, int h, std::vector<Coordinates> &coordinates, std::vector<Coordinates> &white_pixels);

    bool is_white(uint8_t value) {
        return value >= thresh_;
    }

    bool visited(int x, int y) {
        unsigned int normal_pos = y * width_ + x;
        unsigned int byte_pos = normal_pos / 8;

        return lookup_[byte_pos] & (1 << (normal_pos - byte_pos*8));
    }

    void visit(int x, int y) {
        unsigned int normal_pos = y * width_ + x;
        unsigned int byte_pos = normal_pos / 8;

        lookup_[byte_pos] |= (1 << (normal_pos - byte_pos*8));
    }
};

TMaskCleaner::TMaskCleaner(PClip child, int length, int thresh, int fade, IScriptEnvironment* env)
: GenericVideoFilter(child), length_(length), thresh_(thresh), fade_(fade), lookup_(nullptr) {
    if (!vi.IsPlanar()) {
        env->ThrowError("TMaskCleaner: only planar colorspaces are supported!");
    }
    if (length <= 0 || thresh <= 0) {
        env->ThrowError("TMaskCleaner: length and thresh must be greater than zero.");
    }
    if (fade < 0) {
        env->ThrowError("TMaskCleaner: fade cannot be negative.");
    }
    lookup_ = new uint8_t[child->GetVideoInfo().height * child->GetVideoInfo().width / 8];
    width_ = child->GetVideoInfo().width;
}

PVideoFrame TMaskCleaner::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n,env);
    PVideoFrame dst = env->NewVideoFrame(child->GetVideoInfo());

    memset(dst->GetWritePtr(PLANAR_Y), 0, dst->GetPitch(PLANAR_Y) * dst->GetHeight(PLANAR_Y));
    memset(lookup_, 0, child->GetVideoInfo().height * child->GetVideoInfo().width / 8);

    clear_mask(dst->GetWritePtr(PLANAR_Y), src->GetReadPtr(PLANAR_Y), dst->GetRowSize(PLANAR_Y), dst->GetHeight(PLANAR_Y),src->GetPitch(PLANAR_Y), dst->GetPitch(PLANAR_Y));
    return dst;
}

__forceinline void TMaskCleaner::process_pixel(const uint8_t *src, int x, int y, int pitch, int w, int h, std::vector<Coordinates> &coordinates, std::vector<Coordinates> &white_pixels) {
    coordinates.clear();
    white_pixels.clear();

    coordinates.emplace_back(x, y);

    while (!coordinates.empty()) {
        /* pop last coordinates */
        Coordinates current = coordinates.back();
        coordinates.pop_back();

        /* check surrounding positions */
        int x_min = current.first  == 0 ? 0 : current.first - 1;
        int x_max = current.first  == w - 1 ? w : current.first + 2;
        int y_min = current.second == 0 ? 0 : current.second - 1;
        int y_max = current.second == h - 1 ? h : current.second + 2;

        for (int j = y_min; j < y_max; ++j ) {
            for (int i = x_min; i < x_max; ++i ) {
                if (!visited(i,j) && is_white(src[j * pitch + i])) {
                    coordinates.emplace_back(i, j);
                    white_pixels.emplace_back(i, j);
                    visit(i,j);
                }
            }
        }
    }
}

void TMaskCleaner::clear_mask(uint8_t *dst, const uint8_t *src, int w, int h, int src_pitch, int dst_pitch) {
    std::vector<Coordinates> coordinates;
    std::vector<Coordinates> white_pixels;

    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            if (visited(x,y) || !is_white(src[src_pitch * y + x])) {
                continue;
            }
            process_pixel(src, x, y, src_pitch, w,h, coordinates, white_pixels);
            size_t pixels_count = white_pixels.size();
            if (pixels_count >= length_) {
                if ((pixels_count - length_ > fade_) || (fade_ == 0)) {
                    for(auto &pixel: white_pixels) {
                        dst[dst_pitch * pixel.second + pixel.first] = src[src_pitch * pixel.second + pixel.first];
                    }
                } else {
                    for(auto &pixel: white_pixels) {
                        dst[dst_pitch * pixel.second + pixel.first] = src[src_pitch * pixel.second + pixel.first] * (pixels_count - length_) / fade_;
                    }
                }
            }
        }
    }
}

AVSValue __cdecl create_tmaskcleaner(AVSValue args, void*, IScriptEnvironment* env)
{
    enum { CLIP, LENGTH, THRESH, FADE };
    return new TMaskCleaner(args[CLIP].AsClip(), args[LENGTH].AsInt(5), args[THRESH].AsInt(235), args[FADE].AsInt(0), env);
}

const AVS_Linkage *AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    env->AddFunction("TMaskCleaner", "c[length]i[thresh]i[fade]i", create_tmaskcleaner, 0);
    return "Why are you looking at this?";
}
