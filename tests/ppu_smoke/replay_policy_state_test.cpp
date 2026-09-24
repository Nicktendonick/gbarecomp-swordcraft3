// Control-flow/context contract only. Never inspect or compare rendered output.
#include "gba_raster_capture.h"
#include "gba_window_policy.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
static void check(bool ok) { if(!ok) throw std::runtime_error("replay policy state assertion"); }
struct Probe { mutable unsigned calls=0; int action=0; };
static int entry(const void* p,const gba::WsBgMarginContext* c,std::uint16_t* tile) {
    const auto& probe=*static_cast<const Probe*>(p);
    check(c && tile && c->layer==0 && c->output_width==384 && c->native_left==72);
    check(c->screen_y<160 && (c->output_x<72 || c->output_x>=312));
    check(c->io[0x10]==c->screen_y && c->vram[0x100]==0x63);
    ++probe.calls; *tile=0x4080; return probe.action;
}

// Count entry into the regular-BG sampling stage, not rendered colors. A
// rejected layer must never reach this callback, even if its tiles are opaque.
struct WindowProbe {
    mutable std::array<std::array<unsigned,4>,160> calls{};
    std::array<std::uint8_t,160> outside{};
    mutable std::array<unsigned,160> mask_calls{};
    unsigned restriction=31;
};
static unsigned margin_restriction(const void* p,unsigned y,std::uint16_t display,const std::uint8_t* io) {
    const auto& probe=*static_cast<const WindowProbe*>(p);
    check(y<160 && io && (display&0x300)==0x300);
    check(io[0x4a]==probe.outside[y]);
    ++probe.mask_calls[y];
    return probe.restriction;
}
static int window_margin(const void* p,const gba::WsBgMarginContext* c,int* hx) {
    const auto& probe=*static_cast<const WindowProbe*>(p);
    check(c && hx && c->layer<4 && c->screen_y<160);
    check(c->output_width==384 && c->native_left==72);
    check(c->output_x<72 || c->output_x>=312);
    check(c->io[0x4a]==probe.outside[c->screen_y]);
    check(c->io[0x48]==0x3f && c->io[0x49]==0x3f && c->io[0x4b]==0x3f);
    ++probe.calls[c->screen_y][c->layer];
    return -1; // Tile decoding/output are deliberately irrelevant to the test.
}
static WindowProbe* live_probe=nullptr;
static int live_window_margin(const gba::WsBgMarginContext* c,int* hx) {
    return window_margin(live_probe,c,hx);
}
static void test_window_masks() {
    // Existing positional aggregate callers keep their original meaning.
    constexpr gba::GbaReplayViewPolicy legacy{nullptr,nullptr,nullptr,true,true,0,nullptr};
    static_assert(legacy.regular_bg_window_bypass_mask()==0x0f);
    static_assert(gba::GbaReplayViewPolicy{}.regular_bg_window_bypass_mask()==0);
    auto capture=std::make_unique<gba::GbaRasterCapture>();
    auto ppu=std::make_unique<gba::GbaPpu>();
    std::array<std::uint8_t,0x400> io{},oam{},pal{};
    std::array<std::uint8_t,0x18000> vram{};
    std::vector<std::uint8_t> output(384*160*3);
    WindowProbe probe;
    gba::GbaReplayViewPolicy policy;
    policy.user=&probe; policy.bg_margin=window_margin;
    const auto saved_debug=gba::g_ppu_debug_layer_mask;
    const auto saved_authored=gba::g_ws_authored_margin_layers;
    const auto saved_margin=gba::g_ws_bg_margin_provider;
    struct Restore {
        unsigned debug;
        int authored;
        int (*margin)(const gba::WsBgMarginContext*,int*);
        ~Restore() {
            gba::g_ppu_debug_layer_mask=debug;
            gba::g_ws_authored_margin_layers=authored;
            gba::g_ws_bg_margin_provider=margin;
            live_probe=nullptr;
        }
    } restore{saved_debug,saved_authored,saved_margin};
    gba::g_ppu_debug_layer_mask=0x1f;
    auto capture_rows=[&](unsigned display,bool changing_outside=false) {
        capture->reset(); ppu->reset();
        io.fill(0); oam.fill(0); vram.fill(0); pal.fill(0);
        for(unsigned i=0;i<128;++i) oam[i*8+1]=2; // No OBJ-window stencils.
        io[0x40]=177; io[0x41]=57; // WIN0 x=[57,177).
        io[0x42]=210; io[0x43]=20; // WIN1 x=[20,210).
        io[0x44]=160; io[0x46]=160;
        io[0x48]=0x3f; io[0x49]=0x3f; io[0x4b]=0x3f;
        for(unsigned y=0;y<160;++y) {
            // BG2/BG3 and color-effect permissions can change independently.
            probe.outside[y]=changing_outside ? (y%4)*4+(y%2)*32 : 0;
            io[0x4a]=probe.outside[y];
            check(capture->capture({ppu.get(),y,static_cast<std::uint16_t>(display),
                                   io.data(),vram.data(),oam.data(),pal.data()}));
        }
        // Callback state must still come from the immutable captured rows.
        io.fill(0xff); vram.fill(0xff);
    };
    auto check_counts=[&](unsigned expected,bool varying=false) {
        for(unsigned y=0;y<160;++y) {
            const unsigned layers=expected|(varying ? (y%4)*4 : 0);
            for(unsigned bg=0;bg<4;++bg)
                check(probe.calls[y][bg]==((layers&(1u<<bg)) ? 144u : 0u));
        }
    };
    auto run=[&](unsigned expected,bool varying=false) {
        probe.calls={};
        check(capture->draw_view(output.data(),output.size(),384,policy));
        check_counts(expected,varying);
    };

    // Both coordinate windows and OBJ-window fall back to WINOUT in margins.
    for(unsigned windows:{0x2000u,0x4000u,0x8000u,0xe000u}) {
        capture_rows(windows|0x0f00);
        policy.authored_backgrounds=false;
        policy.regular_bg_window_bypass_layers=0;
        run(0); // Default policy respects the captured window layer gates.
        policy.regular_bg_window_bypass_layers=3;
        check(policy.regular_bg_window_bypass_mask()==3);
        run(3); // Scenery may extend without authorizing a hidden BG2 canvas.
        for(unsigned bg=0;bg<4;++bg) {
            policy.regular_bg_window_bypass_layers=1u<<bg;
            run(1u<<bg); // Each bit authorizes only its own regular layer.
        }
        policy.regular_bg_window_bypass_layers=3;
        policy.authored_backgrounds=true;
        run(15); // Legacy Boolean still overrides a narrower new mask.
        policy.authored_backgrounds=false;
        policy.regular_bg_window_bypass_layers=0xfffffff0u;
        check(policy.regular_bg_window_bypass_mask()==0);
        run(0); // Invalid high bits must not authorize any background.
    }
    policy.regular_bg_window_bypass_layers=3;
    capture_rows(0x2f00,true);
    run(3,true); // BG2/BG3 permission follows each row's actual WINOUT.
    policy.regular_bg_window_bypass_layers=0;
    run(0,true);
    // Replay never inherits the live instance's all-layer bypass.
    gba::g_ws_authored_margin_layers=1;
    run(0,true);
    gba::g_ws_authored_margin_layers=saved_authored;

    capture_rows(0x0f00); // No enabled window: every enabled BG is permitted.
    run(15);
    // Per-row restriction cannot be defeated by legacy or per-layer bypass.
    // Calls prove the real sampling path consumes the restriction, using
    // immutable captured registers; no framebuffer is read.
    policy.margin_layer_mask=margin_restriction;
    for(unsigned display:{0x0f00u,0x2f00u,0x2f01u,0x2f02u}) {
        capture_rows(display);
        policy.authored_backgrounds=true;
        for(unsigned mask=0;mask<32;++mask) {
            probe.restriction=mask; probe.mask_calls={};
            run(mask & ((display&7)==0 ? 15u : (display&7)==1 ? 3u : 0u));
            for(unsigned count:probe.mask_calls) check(count==1);
        }
    }
    policy.margin_layer_mask=nullptr;
    policy.authored_backgrounds=false;
    capture_rows(0x2b00); // BG2 disabled in DISPCNT despite all-layer bypass.
    policy.authored_backgrounds=true;
    run(11);
    capture_rows(0x2f00);
    gba::g_ppu_debug_layer_mask=0x15; // BG0/BG2/OBJ only.
    run(5);
    gba::g_ppu_debug_layer_mask=0x1f;
    // The mask does not route affine/bitmap layers through regular callbacks.
    capture_rows(0x2f01); run(3);
    capture_rows(0x2f02); run(0);
    capture_rows(0x2403); run(0);

    // Live callers keep Boolean, not bitmask, semantics for every nonzero value.
    ppu->reset(); ppu->set_view_margins(72,72,0,0);
    io.fill(0); vram.fill(0);
    io[0x40]=177; io[0x41]=57; io[0x44]=160;
    io[0x48]=0x3f; io[0x49]=0x3f; io[0x4b]=0x3f;
    probe.outside.fill(0);
    live_probe=&probe; gba::g_ws_bg_margin_provider=live_window_margin;
    for(int authored:{0,1,2,-1}) {
        gba::g_ws_authored_margin_layers=authored;
        probe.calls={};
        for(unsigned y=0;y<160;++y)
            ppu->render_scanline(y,0x2f00,io.data(),vram.data(),oam.data(),pal.data());
        check_counts(authored ? 15 : 0);
    }
}

static void test_window_control_state() {
    using gba::detail::replay_window_horizontal_contains;
    using gba::detail::replay_window_control;
    for(unsigned mask=0;mask<64;++mask)
        for(unsigned layer=0;layer<5;++layer)
            for(int hx=-72;hx<312;++hx)
                check(gba::detail::replay_margin_layer_allowed(mask,layer,hx,240)==
                      ((hx>=0 && hx<240) || (mask&(1u<<layer))));
    // Every captured horizontal register value retains identical native
    // membership, including wrapped/reversed hardware endpoints. Also verify
    // default margin behavior and a signed extension's half-open endpoints.
    for(unsigned h=0;h<=0xffff;++h) {
        const int x1=(h>>8)&255;
        int x2=h&255;
        if(x2>240 || x1>x2) x2=240;
        for(int hx=-72;hx<312;++hx) {
            const bool native=hx>=x1 && hx<x2;
            check(replay_window_horizontal_contains(h,hx,240,false,-31,281)==native);
            check(replay_window_horizontal_contains(h,hx,240,true,3,3)==native);
            check(replay_window_horizontal_contains(h,hx,240,true,9,-9)==native);
            const bool extended=(hx<0 || hx>=240) ? hx>=-31 && hx<281 : native;
            check(replay_window_horizontal_contains(h,hx,240,true,-31,281)==extended);
        }
    }
    // Every six-bit permission value follows the selected window, including
    // bit 5. The compositor consumes this one result for BG, OBJ and blending.
    for(unsigned mask=0;mask<64;++mask) {
        const unsigned other=mask^0x3f;
        for(unsigned membership=0;membership<8;++membership) {
            const bool w0=membership&1, w1=membership&2, obj=membership&4;
            check(replay_window_control(false,w0,w1,obj,0,0)==0x3f);
            const unsigned winin=mask|(other<<8);
            const unsigned winout=0x11|(0x22<<8);
            const unsigned expected=w0 ? mask : w1 ? other : obj ? 0x22 : 0x11;
            check(replay_window_control(true,w0,w1,obj,winin,winout)==expected);
            check(replay_window_control(true,false,false,true,0,mask<<8)==mask);
            check(replay_window_control(true,false,false,false,0,mask)==mask);
        }
    }
}

struct ExtendedWindowProbe {
    mutable std::array<std::array<unsigned,4>,160> samples{};
    mutable std::array<std::array<unsigned,2>,160> bounds_calls{};
    bool variable_results=false;
};
static bool extended_window_bounds(const void* p,unsigned window,unsigned y,
                                   const std::uint8_t* io,int* left,int* right) {
    const auto& probe=*static_cast<const ExtendedWindowProbe*>(p);
    check(window<2 && y<160 && io && left && right);
    check(io[0x10]==y && io[0x40]==160 && io[0x41]==30);
    check(io[0x42]==220 && io[0x43]==60);
    ++probe.bounds_calls[y][window];
    *left=window ? -60 : -40;
    *right=window ? 300 : 278;
    if(probe.variable_results) {
        if(y%5==1) return false; // Written endpoints do not override false.
        if(y%5==2) *right=*left;
        if(y%5==3) *right=*left-1;
    }
    return true;
}
static int extended_window_sample(const void* p,const gba::WsBgMarginContext* c,int* hx) {
    const auto& probe=*static_cast<const ExtendedWindowProbe*>(p);
    check(c && hx && c->layer<4 && c->screen_y<160);
    check(c->output_width==384 && c->native_left==72);
    check(c->output_x<72 || c->output_x>=312);
    check(c->io[0x10]==c->screen_y);
    ++probe.samples[c->screen_y][c->layer];
    return -1; // Only authorization, not texture/output, is under test.
}
static void test_extended_windows() {
    auto capture=std::make_unique<gba::GbaRasterCapture>();
    auto ppu=std::make_unique<gba::GbaPpu>();
    std::array<std::uint8_t,0x400> io{},oam{},pal{};
    std::array<std::uint8_t,0x18000> vram{};
    std::vector<std::uint8_t> output(384*160*3);
    ExtendedWindowProbe probe;
    gba::GbaReplayViewPolicy policy;
    policy.user=&probe; policy.bg_margin=extended_window_sample;
    const unsigned saved_debug=gba::g_ppu_debug_layer_mask;
    struct Restore { unsigned debug; ~Restore(){gba::g_ppu_debug_layer_mask=debug;} } restore{saved_debug};
    gba::g_ppu_debug_layer_mask=0x1f;
    auto rows=[&](unsigned display) {
        capture->reset(); ppu->reset(); io.fill(0); oam.fill(0);
        for(unsigned i=0;i<128;++i) oam[i*8+1]=2; // No OBJ-window stencils.
        io[0x40]=160; io[0x41]=30;
        io[0x42]=220; io[0x43]=60;
        io[0x44]=150; io[0x45]=10;
        io[0x46]=140; io[0x47]=30;
        for(unsigned y=0;y<160;++y) {
            io[0x10]=y;
            io[0x48]=(y&1) ? 0x24 : 0x05;
            io[0x49]=(y&1) ? 0x28 : 0x0a;
            io[0x4a]=(y&1) ? 0x22 : 0x01;
            io[0x4b]=0x3f;
            check(capture->capture({ppu.get(),y,static_cast<std::uint16_t>(display),
                                   io.data(),vram.data(),oam.data(),pal.data()}));
        }
        io.fill(0xff); // Both callbacks must consume captured, not live, IO.
    };
    auto run=[&](unsigned display,bool callback,unsigned debug=0x1f,unsigned bypass=0) {
        policy.window_margin_bounds=callback ? extended_window_bounds : nullptr;
        policy.regular_bg_window_bypass_layers=bypass;
        gba::g_ppu_debug_layer_mask=debug;
        probe.samples={}; probe.bounds_calls={};
        check(capture->draw_view(output.data(),output.size(),384,policy));
        for(unsigned y=0;y<160;++y) {
            for(unsigned window=0;window<2;++window)
                check(probe.bounds_calls[y][window]==
                      ((callback && (display&(0x2000u<<window))) ? 1u : 0u));
            std::array<unsigned,4> expected{};
            for(int hx=-72;hx<312;++hx) {
                if(hx>=0 && hx<240) continue;
                const bool valid=callback && (!probe.variable_results || y%5==0 || y%5==4);
                const bool w0=valid && (display&0x2000) && y>=10 && y<150 && hx>=-40 && hx<278;
                const bool w1=valid && (display&0x4000) && y>=30 && y<140 && hx>=-60 && hx<300;
                const unsigned mask=!(display&0xe000) ? 0x3f : w0 ? ((y&1) ? 0x24 : 0x05) :
                    w1 ? ((y&1) ? 0x28 : 0x0a) : ((y&1) ? 0x22 : 0x01);
                for(unsigned bg=0;bg<4;++bg)
                    if((display&(0x100u<<bg)) && (debug&(1u<<bg)) && ((mask|bypass)&(1u<<bg)))
                        ++expected[bg];
            }
            for(unsigned bg=0;bg<4;++bg) check(probe.samples[y][bg]==expected[bg]);
        }
    };
    for(unsigned display:{0x0f00u,0x2f00u,0x4f00u,0x6f00u,0xef00u}) {
        rows(display);
        run(display,false); // Null callback preserves captured margin controls.
        probe.variable_results=false;
        run(display,true); // Signed bounds, WIN0 priority, captured vertical limits.
        probe.variable_results=true;
        run(display,true); // False, empty and reversed results are ignored.
        run(display,true,0x1f,3); // Scenery bypass stays independent of BG2 permissions.
        run(display,true,0x1b); // Explicit debug BG2 disable still wins.
    }
    rows(0x6b00);
    run(0x6b00,true); // DISPCNT BG2 disable still wins.
}
int main() {
    try {
        auto capture=std::make_unique<gba::GbaRasterCapture>();
        auto ppu=std::make_unique<gba::GbaPpu>();
        std::array<std::uint8_t,0x400> io{},oam{},pal{};
        std::array<std::uint8_t,0x18000> vram{};
        vram[0x100]=0x63;
        for(unsigned y=0;y<160;++y) {
            io[0x10]=y;
            check(capture->capture({ppu.get(),y,0x0100,io.data(),vram.data(),oam.data(),pal.data()}));
        }
        // Prove callbacks receive owned captured state, not mutable live RAM.
        io.fill(0xff); vram.fill(0xff);
        std::vector<std::uint8_t> output(384*160*3);
        Probe probe;
        gba::GbaReplayViewPolicy policy; policy.user=&probe; policy.text_margin_entry=entry;
        for(int action:{-1,0,1}) {
            probe.action=action;probe.calls=0;
            check(capture->draw_view(output.data(),output.size(),384,policy));
            check(probe.calls==144*160);
        }
        policy.text_margin_entry=nullptr; probe.calls=0;
        check(capture->draw_view(output.data(),output.size(),384,policy)); check(probe.calls==0);
        capture->reset();
        check(!capture->draw_view(output.data(),output.size(),384,policy));
        std::puts("immutable replay margin callback/context/reset: PASS (no pixel assertions)");
        test_window_masks();
        std::puts("per-layer replay windows/legacy policy/row state/enable gates: PASS (no pixel assertions)");
        test_window_control_state();
        test_extended_windows();
        std::puts("signed replay windows/native bounds/priority/six-bit controls: PASS (no pixel assertions)");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
