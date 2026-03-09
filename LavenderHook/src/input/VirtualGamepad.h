#pragma once

#include <Windows.h>

namespace LavenderHook::Input::VGamepad {

    bool Initialize();
    bool Available();

    void SetButton(int gpvk, bool down);
    void Press(int gpvk);
    void SetThumb(bool leftStick, float nx, float ny);

    bool AutomationActive();

    bool Initializing();

    DWORD GetUserIndex();

    void Update();
    bool Failed();
    void Shutdown();
}
