// Standalone regression tests for the shared software/OpenGL screen transforms.
// Build: c++ -std=c++17 tests/frontend/single_screen_layout.cpp \
//        src/frontend/ScreenLayout.cpp -o build/single_screen_layout_test
#include "../../src/frontend/ScreenLayout.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    int cases = 0;
    ScreenLayout layout;
    for (int arrangement = 0; arrangement < screenLayout_MAX; ++arrangement)
    for (int rotation = 0; rotation < screenRot_MAX; ++rotation)
    for (bool swapped : {false, true})
    for (bool integerScale : {false, true})
    for (float aspect : {1.f, (16.f / 9.f) / (4.f / 3.f)})
    {
        for (auto sizing : {screenSizing_TopOnly, screenSizing_BotOnly, screenSizing_TopOnly})
        {
            layout.Setup(1280, 720, static_cast<ScreenLayoutType>(arrangement),
                         static_cast<ScreenRotation>(rotation), sizing,
                         32, integerScale, swapped, aspect, aspect);
            float transforms[kMaxScreenTransforms * 6];
            int kinds[kMaxScreenTransforms];
            require(layout.GetScreenTransforms(transforms, kinds) == 1,
                    "Single-screen mode must emit exactly one transform, including Hybrid");
            require(kinds[0] == (sizing == screenSizing_BotOnly ? 1 : 0),
                    "Wrong framebuffer selected during top/bottom/top transition");
            for (int i = 0; i < 6; ++i)
                require(std::isfinite(transforms[i]), "Non-finite screen transform");

            // Transform a DS coordinate into the viewport, then invert it through
            // the same touchscreen path used by mouse, pen, and touch events.
            int x = std::lround(transforms[0] * 128 + transforms[2] * 96 + transforms[4]);
            int y = std::lround(transforms[1] * 128 + transforms[3] * 96 + transforms[5]);
            bool touchable = layout.GetTouchCoords(x, y, false);
            require(touchable == (sizing == screenSizing_BotOnly),
                    "Only the visible bottom screen may accept touchscreen input");
            if (touchable)
                require(std::abs(x - 128) <= 1 && std::abs(y - 96) <= 1,
                        "Touch coordinates must survive rotation/aspect/scaling");
            ++cases;
        }
    }
    // Existing dual-screen modes must retain both framebuffers (three in Hybrid).
    for (int arrangement = 0; arrangement < screenLayout_MAX; ++arrangement)
    {
        layout.Setup(1280, 720, static_cast<ScreenLayoutType>(arrangement),
                     screenRot_0Deg, screenSizing_Even, 0, false, false, 1, 1);
        float transforms[kMaxScreenTransforms * 6];
        int kinds[kMaxScreenTransforms];
        require(layout.GetScreenTransforms(transforms, kinds)
                    == (arrangement == screenLayout_Hybrid ? 3 : 2),
                "Dual-screen behavior changed");
        ++cases;
    }
    std::cout << cases << " screen-layout cases passed\n";
}
