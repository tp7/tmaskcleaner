#include <Windows.h>
#include <vector>
#pragma warning(disable: 4512 4244 4100)
#include "avisynth.h"
#pragma warning(default: 4512 4244 4100)

using namespace std;

typedef pair<int, int> Coordinates;

class TMaskCleaner : public GenericVideoFilter {
public:
    TMaskCleaner(PClip child, int length, int thresh, int fade, IScriptEnvironment*);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

    ~TMaskCleaner() {
        if (lookup != nullptr) {
            delete[] lookup;
        }
    }
private:
    unsigned int m_length;
    unsigned int m_thresh;
    unsigned int m_fade;
    BYTE *lookup;
    int m_w;

    void ClearMask(BYTE *dst, const BYTE *src, int width, int height, int src_pitch, int dst_pitch);
    void ProcessPixel(const BYTE *src, int x, int y, int pitch, int w, int h, vector<Coordinates> &coordinates, vector<Coordinates> &white_pixels);

    bool IsWhite(BYTE value) {
        return value >= m_thresh;
    }

    bool Visited(int x, int y) {
        unsigned int normal_pos = y * m_w + x;
        unsigned int byte_pos = normal_pos / 8;

        return lookup[byte_pos] & (1 << (normal_pos - byte_pos*8));
    }

    void Visit(int x, int y) {
        unsigned int normal_pos = y * m_w + x;
        unsigned int byte_pos = normal_pos / 8;

        lookup[byte_pos] |= (1 << (normal_pos - byte_pos*8));
    }
};

TMaskCleaner::TMaskCleaner(PClip child, int length, int thresh, int fade, IScriptEnvironment* env) : GenericVideoFilter(child), m_length(length), m_thresh(thresh), m_fade(fade), lookup(nullptr) {
    if (!vi.IsPlanar()) {
        env->ThrowError("Only YV12 and YV24 is supported!");
    }
    if (length <= 0 || thresh <= 0) {
        env->ThrowError("Invalid arguments!");
    }
    lookup = new BYTE[child->GetVideoInfo().height * child->GetVideoInfo().width / 8];
    m_w = child->GetVideoInfo().width;
}

PVideoFrame TMaskCleaner::GetFrame(int n, IScriptEnvironment* env) {
    PVideoFrame src = child->GetFrame(n,env);
    PVideoFrame dst = env->NewVideoFrame(child->GetVideoInfo());

    memset(dst->GetWritePtr(PLANAR_Y), 0, dst->GetPitch(PLANAR_Y) * dst->GetHeight(PLANAR_Y));
    memset(lookup, 0, child->GetVideoInfo().height * child->GetVideoInfo().width / 8);

    ClearMask(dst->GetWritePtr(PLANAR_Y), src->GetReadPtr(PLANAR_Y), dst->GetRowSize(PLANAR_Y), dst->GetHeight(PLANAR_Y),src->GetPitch(PLANAR_Y), dst->GetPitch(PLANAR_Y));
    return dst;
}

__forceinline void TMaskCleaner::ProcessPixel(const BYTE *src, int x, int y, int pitch, int w, int h, vector<Coordinates> &coordinates, vector<Coordinates> &white_pixels) {
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
                if (!Visited(i,j) && IsWhite(src[j * pitch + i])) {
                    coordinates.emplace_back(i, j);
                    white_pixels.emplace_back(i, j);
                    Visit(i,j);
                }
            }
        }
    }
}

void TMaskCleaner::ClearMask(BYTE *dst, const BYTE *src, int w, int h, int src_pitch, int dst_pitch) {
    vector<Coordinates> coordinates;
    vector<Coordinates> white_pixels;

    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            if (Visited(x,y) || !IsWhite(src[src_pitch * y + x])) {
                continue;
            }
            ProcessPixel(src, x, y, src_pitch, w,h, coordinates, white_pixels);
            if (white_pixels.size() >= m_length) {
				if ((white_pixels.size() - m_length > m_fade) || (m_fade <= 0)) {
					for(auto &pixel: white_pixels) {
						dst[dst_pitch * pixel.second + pixel.first] = src[src_pitch * pixel.second + pixel.first];
					}
				}
				else {
					for(auto &pixel: white_pixels) {
						dst[dst_pitch * pixel.second + pixel.first] = src[src_pitch * pixel.second + pixel.first] * (white_pixels.size() - m_length) / m_fade;
					}
				}
            }
        }
    }
}

AVSValue __cdecl Create_TMaskCleaner(AVSValue args, void*, IScriptEnvironment* env) 
{
    enum { CLIP, LENGTH, THRESH, FADE};
    return new TMaskCleaner(args[CLIP].AsClip(), args[LENGTH].AsInt(5), args[THRESH].AsInt(235), args[FADE].AsInt(0), env);
}

const AVS_Linkage *AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;

    env->AddFunction("TMaskCleaner", "c[length]i[thresh]i[fade]i", Create_TMaskCleaner, 0);
    return "Why are you looking at this?";
}
