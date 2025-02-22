#include <android/log.h>
#include <dlfcn.h>
#include <enet/enet.h>

#include "Hook.h"
#include "include/Dobby/dobby.h"
#include "packet/TextParse.h"
#include "packet/Decoder.h"
#include "utilities/Macros.h"

game::Game *g_game{ nullptr };
ui::UI *g_ui{ nullptr };
enet_uint16 g_port{ 65535 };

void (*BaseApp_Draw)(void *thiz);
void BaseApp_Draw_hook(void *thiz) {
    BaseApp_Draw(thiz);

    static bool initialized{ false };
    if (!initialized) {
        g_game = new game::Game{};
        g_game->init();

        auto width = KittyMemory::callFunction<float>(GTS("_Z15GetScreenSizeXfv"));
        auto height = KittyMemory::callFunction<float>(GTS("_Z15GetScreenSizeYfv"));
        g_ui = new ui::UI{ ImVec2(width, height) };
        g_ui->initialize();

        initialized = true;
    }
    else {
        if (g_ui) {
            g_ui->render();
        }

        ui::UI::handle_input();
    }
}

void (*BaseApp_SetFPSLimit)(void *thiz, float fps);
void BaseApp_SetFPSLimit_hook(void *thiz, float fps) {
    if (g_game) {
        BaseApp_SetFPSLimit(thiz, g_game->m_fpsLimit);
    }
    else {
        BaseApp_SetFPSLimit(thiz, fps);
    }
}

void (*AppOnTouch)(void *a1, void *a2, int type, float x, float y, bool multi);
void AppOnTouch_hook(void *a1, void *a2, int type, float x, float y, bool multi) {
    ImGuiIO& io = ImGui::GetIO();

    if (g_ui && (x > 0.0 || y > 0.0)) {
        g_ui->on_touch(type, multi, x, y);
    }

    if (!&io || !io.WantCaptureMouse) {
        AppOnTouch(a1, a2, type, x, y, multi);
    }
    else {
        AppOnTouch(a1, a2, 1, 0.0f, 0.0f, false);
    }
}

void (*AppOnKey)(void *, void *, int, int, int);
void AppOnKey_hook(void *a1, void *a2, int type, int keycode, int c) {
    ImGuiContext& g = *GImGui;

    // Make sure if keyboard is not opened because of imgui, we don't handle it.
    if (g.ActiveId == 0) {
        return AppOnKey(a1, a2, type, keycode, c);
    }

    switch (keycode) {
        case 66: // Enter
            c = 13;
            break;
        case 67: // Backspace
            c = 8;
            break;
        case 113: // CTRL
            return;
        default:
            break;
    }

    ImGuiIO &io = ImGui::GetIO();
    ImGuiInputTextState *state = ImGui::GetInputTextState(g.ActiveId);
    if (type == 1) {
        switch (c) {
            case 0: // No char
            case 9: // Tab
                break;
            case 8: { // Backspace
                if (state) {
                    state->ClearText();
                }

                io.InputQueueCharacters.clear();
                break;
            }
            case 13: // Enter
            case 27: // Escape
                ImGui::ClearActiveID();
                break;
            default: {
                io.AddInputCharacter(c);
                break;
            }
        }
    }

    AppOnKey(a1, a2, type, keycode, c);
}

void (*NetHTTP_Update)(NetHTTP *thiz);
void NetHTTP_Update_hook(NetHTTP *thiz) {
    NetHTTP_Update(thiz);
}

void (*ENetClient_Init)(uintptr_t thiz, std::string a1, enet_uint16 port, std::string a2);
void ENetClient_Init_hook(uintptr_t thiz, std::string a1, enet_uint16 port, std::string a2) {
    if (g_port == 65535) {
        g_port = port;
    }

    ENetClient_Init(thiz, std::move(a1), port, std::move(a2));
}

void (*App_Update)(void* thiz);
void App_Update_hook(void* thiz) {
    App_Update(thiz);
}

void (*enet_peer_sends)(ENetPeer* peer, enet_uint8 a2, ENetPacket* packet);
void enet_peer_send_hook(ENetPeer* peer, enet_uint8 a2, ENetPacket* packet) {
    memset(packet->data + packet->dataLength - 1, 0, 1);
    char* data = (char*)(packet->data + 4);

    std::string modified_packet = std::string(data);
    packet::TextParse text_parse(modified_packet);
    if (text_parse.get_line_count() != 0) {
        if (!text_parse.get("requestedName", 0).empty()) {
            // textParse.Set("game_version", "3.75");
            text_parse.get_all_raw(modified_packet);
        }
    }
#if 0
    GameUpdatePacket* gameUpdatePacket = Packet::Decoder::GetStructPointerFromTankPacket(packet);
    if (gameUpdatePacket) {
        LOGD("gameUpdatePacket type: %d", (int)gameUpdatePacket->packetType);
        LOGD("unk0: %d, unk1: %d, unk2: %d, unk3: %d, unk5: %d", gameUpdatePacket->unk0, gameUpdatePacket->unk1, gameUpdatePacket->unk2, gameUpdatePacket->unk4, gameUpdatePacket->unk5);
        LOGD("unk6: %d, unk7: %d, unk8: %d, unk9: %f, unk10: %f", gameUpdatePacket->unk6, gameUpdatePacket->unk7, gameUpdatePacket->unk8, gameUpdatePacket->unk9, gameUpdatePacket->unk10);
        LOGD("unk6: %f, unk7: %f, unk8: %f, unk9: %d, unk10: %d", gameUpdatePacket->unk11, gameUpdatePacket->unk12, gameUpdatePacket->unk13, gameUpdatePacket->unk14, gameUpdatePacket->unk15);
    }
    else {
        LOGD("Type: %d, modifiedPacket: %s", *(packet->data), modifiedPacket.c_str());
    }
#endif
    memcpy(packet->data + 4, modified_packet.c_str(), modified_packet.length());
    enet_peer_sends(peer, a2, packet);
}

void (*GameLogicComponent_SendPing)(void *thiz, void *a2);
void GameLogicComponent_SendPing_hook(void *thiz, void *a2) {
    GameLogicComponent_SendPing(thiz, a2);
}

namespace game {
    namespace hook {
        void init() {
            LOGD("Initializing Hook..");

            // set Dobby logging level.
            log_set_level(0);

            // BaseApp::Draw()
            DobbyHook(GTS("_ZN7BaseApp4DrawEv"), (void *)BaseApp_Draw_hook, (void **)&BaseApp_Draw);

            // BaseApp::SetFPSLimit()
            // DobbyHook(GTS("_ZN7BaseApp11SetFPSLimitEf"), (void *)BaseApp_SetFPSLimit_hook, (void **)&BaseApp_SetFPSLimit);

            // AppOnTouch()
            DobbyHook(GTS("_Z10AppOnTouchP7_JNIEnvP8_jobjectiffi"), (void *)AppOnTouch_hook, (void **)&AppOnTouch);

            // AppOnKey()
            DobbyHook(GTS("_Z8AppOnKeyP7_JNIEnvP8_jobjectiii"), (void *)AppOnKey_hook, (void **)&AppOnKey);

            // NetHTTP::Update()
            // DobbyHook(GTS("_ZN7NetHTTP6UpdateEv"), (void *)NetHTTP_Update_hook, (void **)&NetHTTP_Update);

            // ENetClient::Init()
            // DobbyHook(GTS("_ZN10ENetClient4InitESsiSs"), (void *)ENetClient_Init_hook, (void **)&ENetClient_Init);

            // App::Update()
            // DobbyHook(GTS("_ZN3App6UpdateEv"), (void *)App_Update_hook, (void **)&App_Update);

            // enet_peer_send()
            // DobbyHook(GTS("enet_peer_send"), (void *)enet_peer_send_hook, (void **)&enet_peer_sends);

            // GameLogicComponent::SendPing()
            // DobbyHook(GTS("_ZN18GameLogicComponent8SendPingEv"), (void *)GameLogicComponent_SendPing_hook, (void **)&GameLogicComponent_SendPing);
        }
    } // namespace hook
} // namespace game