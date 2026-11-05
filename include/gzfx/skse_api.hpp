#pragma once

#include <cstddef>
#include <cstdint>

namespace gzfx::skse
{
    using PluginHandle = std::uint32_t;

    inline constexpr PluginHandle kInvalidPluginHandle = 0xFFFFFFFFu;

    enum InterfaceId : std::uint32_t
    {
        Invalid = 0,
        Scaleform = 1,
        Papyrus = 2,
        Serialization = 3,
        Task = 4,
        Messaging = 5,
        Object = 6,
        Trampoline = 7
    };

    struct PluginInfo
    {
        std::uint32_t infoVersion;
        const char* name;
        std::uint32_t version;
    };

    struct Interface
    {
        std::uint32_t skseVersion;
        std::uint32_t runtimeVersion;
        std::uint32_t editorVersion;
        std::uint32_t isEditor;

        void* (*QueryInterface)(std::uint32_t id);
        PluginHandle (*GetPluginHandle)();
        std::uint32_t (*GetReleaseIndex)();
        const PluginInfo* (*GetPluginInfo)(const char* name);
    };

    struct MessagingInterface
    {
        struct Message
        {
            const char* sender;
            std::uint32_t type;
            std::uint32_t dataLen;
            void* data;
        };

        using EventCallback = void(*)(Message* message);

        enum MessageType : std::uint32_t
        {
            PostLoad = 0,
            PostPostLoad = 1,
            PreLoadGame = 2,
            PostLoadGame = 3,
            SaveGame = 4,
            DeleteGame = 5,
            InputLoaded = 6,
            NewGame = 7,
            DataLoaded = 8
        };

        std::uint32_t interfaceVersion;
        bool (*RegisterListener)(
            PluginHandle listener,
            const char* sender,
            EventCallback handler);
        bool (*Dispatch)(
            PluginHandle sender,
            std::uint32_t messageType,
            void* data,
            std::uint32_t dataLen,
            const char* receiver);
        void* (*GetEventDispatcher)(
            std::uint32_t dispatcherId);
    };

    struct TrampolineInterface
    {
        std::uint32_t interfaceVersion;
        void* (*AllocateFromBranchPool)(
            PluginHandle plugin,
            std::size_t size);
        void* (*AllocateFromLocalPool)(
            PluginHandle plugin,
            std::size_t size);
    };

    struct PluginVersionData
    {
        static constexpr std::uint32_t kVersion = 1;

        std::uint32_t dataVersion;
        std::uint32_t pluginVersion;
        char name[256];
        char author[256];
        char supportEmail[252];
        std::uint32_t versionIndependenceEx;
        std::uint32_t versionIndependence;
        std::uint32_t compatibleVersions[16];
        std::uint32_t seVersionRequired;
    };

    constexpr std::uint32_t MakeExeVersionEx(
        const std::uint32_t major,
        const std::uint32_t minor,
        const std::uint32_t build,
        const std::uint32_t sub = 0) noexcept
    {
        return
            ((major & 0xFFu) << 24u) |
            ((minor & 0xFFu) << 16u) |
            ((build & 0xFFFu) << 4u) |
            (sub & 0xFu);
    }

    inline constexpr std::uint32_t kRuntime_1_7_104 =
        MakeExeVersionEx(1, 7, 104);

    inline constexpr std::uint32_t kSkse_2_3_1 =
        MakeExeVersionEx(2, 3, 1);

    inline constexpr std::uint32_t kSkseReleaseIndex_2_3_1 = 75;

    static_assert(sizeof(Interface) == 48);
    static_assert(sizeof(MessagingInterface) == 32);
    static_assert(sizeof(TrampolineInterface) == 24);
    static_assert(sizeof(PluginVersionData) == 848);
}
