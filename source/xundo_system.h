#ifndef XUNDO_H
#define XUNDO_H
#pragma once

#include "xcmdline_parser.h"
#include <format>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <chrono>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <list>
#include <filesystem>
#include <cassert>
#include <span>
#include <array>

namespace xundo
{
    class   system;
    struct  command_base;
    class   history;
    namespace system_example { int StressTest(); }

    //-----------------------------------------------------------------------------------------------------------
    // History Entry: Represents a single undo/redo step, including groups
    //-----------------------------------------------------------------------------------------------------------
    struct sub_command
    {
        std::string             m_CommandString;    // Command string (e.g., "Move -T 10 20")
        int                     m_DataOffset;       // Offset in the file to read its data
    };

    struct history_entry
    {
        mutable std::mutex                  m_Mutex;                // Mutex to protect the entire entry
        int                                 m_UserID;               // User ID for the step
        std::uint64_t                       m_TimeStamp;            // Time stamp for the step
        std::string                         m_CommandString;        // Command string (e.g., "Group MyGroup" or "Move -T 10 20")
        std::vector<std::byte>              m_CacheUndoData;        // Cache undo data for all sub-commands
        bool                                m_bHasBeenSaved = false;// Has this entry been saved to disk
        std::vector<sub_command>            m_SubCommands;          // Sub-command strings for groups (empty if not a group)

        // Helper to check if this is a group
        bool IsGroup() const noexcept { return !m_SubCommands.empty(); }
    };

    //-----------------------------------------------------------------------------------------------------------
    // Undo File: Manages reading/writing undo data to/from the cache
    //-----------------------------------------------------------------------------------------------------------
    struct undo_file
    {
        history_entry&  m_Entry;                // Reference to the history entry
        std::uint32_t   m_Index;                // Current index in m_CacheUndoData

             undo_file(history_entry& Entry, std::uint32_t Index = 0)
                : m_Entry(Entry), m_Index(Index) {}

        // Writes raw data to cache
        void Write(const void* pData, std::uint64_t Size) noexcept
        {
            auto& Cache = m_Entry.m_CacheUndoData;
            assert(pData);
            Cache.insert(Cache.begin() + m_Index, reinterpret_cast<const std::byte*>(pData), reinterpret_cast<const std::byte*>(pData) + Size);
            m_Index += static_cast<std::uint32_t>(Size);
        }

        // Writes typed data to cache
        template<typename T>
        void Write(const T& Data) noexcept
        {
            Write(&Data, sizeof(T));
        }

        // Reads raw data from cache
        void Read(void* pData, std::uint64_t Size) noexcept
        {
            auto& Cache = m_Entry.m_CacheUndoData;
            assert(pData && m_Index + Size <= Cache.size());
            std::memcpy(pData, Cache.data() + m_Index, Size);
            m_Index += static_cast<std::uint32_t>(Size);
        }

        // Reads typed data from cache
        template<typename T>
        void Read(T& Data) noexcept
        {
            Read(&Data, sizeof(T));
        }
    };

    //-----------------------------------------------------------------------------------------------------------
    // Job Namespace: Asynchronous I/O operations for history entries
    //-----------------------------------------------------------------------------------------------------------
    namespace job
    {
        // Base class for all I/O jobs
        struct base
        {
            virtual      ~base      () noexcept = default;
            virtual void  Execute   () noexcept = 0;
        };

        // Saves a history entry to disk
        struct save_to_disk final : base
        {
            save_to_disk(system& System, std::shared_ptr<history_entry> Entry) noexcept
                : m_System(System), m_Entry(Entry) {
            }

            // Writes entry data and sub-commands to disk
            void Execute() noexcept override;

            // Saves entry to file, including sub-commands
            static bool Save(const history_entry& Entry, std::string_view Path) noexcept
            {
                FILE* File;
                if (auto Err = fopen_s(&File, std::format("{}/UndoStep-{}", Path, Entry.m_TimeStamp).c_str(), "wb"); Err)
                {
                    char                ErrMsg[100];
                    strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                    std::printf("Error: %s\n", ErrMsg);
                    return false;
                }
                bool                    Ok = true;

                uint32_t                DataLen = static_cast<uint32_t>(Entry.m_CacheUndoData.size());
                Ok &= fwrite(&DataLen, sizeof(uint32_t), 1, File) == 1;
                Ok &= fwrite(Entry.m_CacheUndoData.data(), DataLen, 1, File) == 1;
                Ok &= fwrite(&Entry.m_UserID, sizeof(int), 1, File) == 1;
                Ok &= fwrite(&Entry.m_TimeStamp, sizeof(uint64_t), 1, File) == 1;
                uint32_t                StrLen = static_cast<uint32_t>(Entry.m_CommandString.size());
                Ok &= fwrite(&StrLen, sizeof(uint32_t), 1, File) == 1;
                Ok &= fwrite(Entry.m_CommandString.data(), StrLen, 1, File) == 1;

                uint32_t                SubCmdCount = static_cast<uint32_t>(Entry.m_SubCommands.size());
                Ok &= fwrite(&SubCmdCount, sizeof(uint32_t), 1, File) == 1;
                for (const auto& SubCmd : Entry.m_SubCommands)
                {
                    uint32_t SubStrLen = static_cast<uint32_t>(SubCmd.m_CommandString.size());
                    Ok &= fwrite(&SubStrLen, sizeof(uint32_t), 1, File) == 1;
                    Ok &= fwrite(SubCmd.m_CommandString.data(), SubStrLen, 1, File) == 1;
                    Ok &= fwrite(&SubCmd.m_DataOffset, sizeof(int), 1, File) == 1;
                }

                fclose(File);
                return Ok;
            }

            system& m_System;
            std::shared_ptr<history_entry> m_Entry;
        };

        // Deletes history entries from disk
        struct delete_entries final : base
        {
            delete_entries(system& System, std::vector<std::uint64_t>&& TimeStamps) noexcept
                : m_System(System), m_TimeStamps(std::move(TimeStamps)) {
            }

            // Removes entry files from disk
            void Execute() noexcept;

            system& m_System;
            std::vector<std::uint64_t>  m_TimeStamps;
        };

        // Loads history entry cache from disk
        struct warmup_cache final : base
        {
            warmup_cache(system& System, std::shared_ptr<history_entry> Entry) noexcept
                : m_System(System), m_Entry(Entry) {
            }

            // Fetches undo data into cache
            void Execute() noexcept override;

            // Loads entry data—key or cache selectable
            static bool Load(history_entry& Entry, std::string_view Path, bool bLoadKeyData, bool bLoadCacheData) noexcept
            {
                FILE* File;
                if (auto Err = fopen_s(&File, std::format("{}/UndoStep-{}", Path, Entry.m_TimeStamp).c_str(), "rb"); Err)
                {
                    char                ErrMsg[100];
                    strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                    std::printf("Error: %s\n", ErrMsg);
                    return false;
                }

                bool                    Ok = true;
                uint32_t                DataLen;
                Ok &= fread(&DataLen, sizeof(uint32_t), 1, File) == 1;

                if (bLoadCacheData)
                {
                    Entry.m_CacheUndoData.resize(DataLen);
                    Ok &= fread(Entry.m_CacheUndoData.data(), DataLen, 1, File) == 1;
                }
                else
                {
                    std::fseek(File, DataLen, SEEK_CUR);
                }

                if (bLoadKeyData)
                {
                    Ok &= fread(&Entry.m_UserID, sizeof(int), 1, File) == 1;
                    Ok &= fread(&Entry.m_TimeStamp, sizeof(uint64_t), 1, File) == 1;
                    uint32_t            StrLen;
                    Ok &= fread(&StrLen, sizeof(uint32_t), 1, File) == 1;
                    Entry.m_CommandString.resize(StrLen);
                    Ok &= fread(Entry.m_CommandString.data(), StrLen, 1, File) == 1;

                    uint32_t            SubCmdCount;
                    Ok &= fread(&SubCmdCount, sizeof(uint32_t), 1, File) == 1;
                    Entry.m_SubCommands.resize(SubCmdCount);
                    for (uint32_t i = 0; i < SubCmdCount; ++i)
                    {
                        uint32_t        SubStrLen;
                        Ok &= fread(&SubStrLen, sizeof(uint32_t), 1, File) == 1;
                        Entry.m_SubCommands[i].m_CommandString.resize(SubStrLen);
                        Ok &= fread(Entry.m_SubCommands[i].m_CommandString.data(), SubStrLen, 1, File) == 1;
                        Ok &= fread(&Entry.m_SubCommands[i].m_DataOffset, sizeof(int), 1, File) == 1;
                    }
                }

                fclose(File);
                return Ok;
            }

            system& m_System;
            std::shared_ptr<history_entry> m_Entry;
        };

        // Loads history entry key data from disk
        struct load_entries final : base
        {
            load_entries(system& System, std::shared_ptr<history_entry> Entry) noexcept
                : m_System(System), m_Entry(Entry) {
            }

            // Fetches key data (user, timestamp, strings) into entry
            void Execute() noexcept override;

            system& m_System;
            std::shared_ptr<history_entry> m_Entry;
        };
    }

    //-----------------------------------------------------------------------------------------------------------
    // Command Base: Interface for all undoable commands
    //-----------------------------------------------------------------------------------------------------------
    struct command_base
    {
        // Constructor: Links command to system and database
        command_base(system& System, const char* pName, void* pDataBase) noexcept;

        // Gets typed reference to database
        template<typename T> T& get() noexcept
        {
            return *static_cast<T*>(m_pDataBase);
        }

        // Virtual methods for command behavior
        virtual const char*     getCommandHelp      ()          const   noexcept = 0;
        virtual void            RegisterArguments   ()                  noexcept = 0;
        virtual std::string     Redo                ()                  noexcept = 0;
        virtual void            Undo                (undo_file& File)   noexcept = 0;
        virtual void            BackupCurrenState   (undo_file& File)   noexcept = 0;

        // Parses command string into arguments
        std::string             Parse(std::string_view cmd_str) noexcept
        {
            m_Parser.clearArgs();
            return m_Parser.Parse(cmd_str);
        }

        // Members: Core command data
        system&                  m_System;
        xcmdline::parser         m_Parser       = {};
        const char*              m_pCommandName = {};
        void*                    m_pDataBase    = {};
        xcmdline::parser::handle m_hHelp        = {};
    };

    //-----------------------------------------------------------------------------------------------------------
    // Utility: Extracts command name from a string
    //-----------------------------------------------------------------------------------------------------------
    std::string_view getCommandName(std::string_view str)
    {
        size_t pos = str.find(' ');
        return pos == std::string_view::npos ? str : str.substr(0, pos);
    }

    //-----------------------------------------------------------------------------------------------------------
    // System: Core undo/redo manager with thread-safe I/O
    //-----------------------------------------------------------------------------------------------------------
    class system
    {
    public:
        //-------------------------------------------------------------------------------------------------------
        // Construction/Destruction
        //-------------------------------------------------------------------------------------------------------
        system() = default;

        // Cleans up, saves if needed, shuts down I/O threads
        ~system() noexcept
        {
            if (!m_UndoPath.empty() && m_bAutoLoadSave)
            {
                if (auto Err = SaveTimestamps(); !Err.empty())
                {
                    std::cerr << Err << "\n";
                }
            }
            if (!m_Done)
            {
                // Signal all threads to exit
                {
                    std::lock_guard<std::mutex> Lock(m_Mutex);
                    m_Done = true;
                }

                // Tell them to check the signal
                m_Cond.notify_all();

                // wait for all threads to finish
                for (auto& E : m_IOThread) E.join();
            }
        }

        //-------------------------------------------------------------------------------------------------------
        // Initialization
        //-------------------------------------------------------------------------------------------------------
        // Sets up the system, optionally loads prior history
        [[nodiscard]] std::string Init(std::string_view UndoPath = {}, bool bAutoLoadSave = true, std::uint32_t MaxUndoSteps = 1000 ) noexcept
        {
            m_UndoPath      = UndoPath;
            m_bAutoLoadSave = bAutoLoadSave;
            m_Done          = false;
            m_MaxUndoSteps  = MaxUndoSteps;

            if (!UndoPath.empty())
            {
                for (int i = 0; i < 4; ++i) m_IOThread.emplace_back(std::thread(&system::IOWorker, std::ref(*this)));
                if (m_bAutoLoadSave)
                {
                    std::string     Path = std::format("{}/UndoTimestamps.bin", m_UndoPath);
                    if (std::filesystem::exists(Path))
                    {
                        return LoadTimestamps(Path);
                    }
                }
            }
            else
            {
                assert(!m_bAutoLoadSave);
            }
            return {};
        }

        //-------------------------------------------------------------------------------------------------------
        // Command Execution
        //-------------------------------------------------------------------------------------------------------
        // Executes a single command by name
        [[nodiscard]] std::string Execute(std::string_view cmd_str, int UserID = -1)
        {
            assert(!m_Done);
            auto Name  = getCommandName(cmd_str);
            auto CmdIt = m_Commands.find(std::string(Name));

            if (CmdIt == m_Commands.end()) 
                return std::format("Unable find the command: {}", Name);

            return Execute(*CmdIt->second, cmd_str, UserID);
        }

        // Executes a group of commands as one step
        [[nodiscard]] std::string Execute(std::string_view group_name, const std::vector<std::string>& Cmds, int UserID = -1) noexcept
        {
            assert(!m_Done);
            if (Cmds.empty()) return "Group needs commands!";
 
            if (UserID == -1) UserID = m_DefaultUser;

            auto Entry = std::make_shared<history_entry>();
            Entry->m_UserID        = UserID;
            Entry->m_TimeStamp     = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() * 1000 + m_CommandCounter++;
            Entry->m_CommandString = std::string(group_name);
            Entry->m_SubCommands.resize(Cmds.size());

            // Execute and backup all sub-commands
            {
                undo_file File(*Entry);
                for (const auto& CmdStr : Cmds)
                {
                    int  Index = static_cast<int>(&CmdStr - Cmds.data());
                    auto Name  = getCommandName(CmdStr);
                    auto CmdIt = m_Commands.find(std::string(Name));

                    if (CmdIt == m_Commands.end()) return std::format("Unknown command: {}", Name);
                    auto& SubCmd = *CmdIt->second;

                    if (auto Err = SubCmd.Parse(CmdStr); !Err.empty()) return Err;
                    if (SubCmd.m_Parser.hasOption(SubCmd.m_hHelp))
                    {
                        return "A group can not have a help command...";
                    }

                    auto& SubCmdEntry = Entry->m_SubCommands[Index];
                    SubCmdEntry.m_CommandString = CmdStr;
                    SubCmdEntry.m_DataOffset    = File.m_Index;
                    SubCmd.BackupCurrenState(File);
                    if (auto Err = SubCmd.Redo(); !Err.empty()) return Err;
                }
            }

            PruneHistory();
            PruneOldSteps();
            m_History.push_back(Entry);
            m_UndoIndex++;
            if (!m_UndoPath.empty())
            {
                PushJob(std::make_unique<job::save_to_disk>(*this, Entry));
                m_LRU.push_back(Entry);
                UpdateLRU();
            }
            return {};
        }

        // Executes a single command instance
        [[nodiscard]] std::string Execute(command_base& Cmd, std::string_view cmd_str, int UserID = -1) noexcept
        {
            assert(!m_Done);

            if (auto Err = Cmd.Parse(cmd_str); !Err.empty()) return Err;
            if (Cmd.m_Parser.hasOption(Cmd.m_hHelp))
            {
                Cmd.m_Parser.printHelp();
                return {};
            }

            if (UserID == -1) UserID = m_DefaultUser;

            auto Entry = std::make_shared<history_entry>();
            Entry->m_UserID        = UserID;
            Entry->m_TimeStamp     = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() * 1000 + m_CommandCounter++;
            Entry->m_CommandString = cmd_str;

            {
                undo_file           File(*Entry);
                Cmd.BackupCurrenState(File);
            }
            if (auto Err = Cmd.Redo(); !Err.empty()) return Err;

            PruneHistory();
            PruneOldSteps();
            m_History.push_back(Entry);
            m_UndoIndex++;
            if (!m_UndoPath.empty())
            {
                PushJob(std::make_unique<job::save_to_disk>(*this, Entry));
                m_LRU.push_back(Entry);
                UpdateLRU();
            }
            return {};
        }

        //-------------------------------------------------------------------------------------------------------
        // Undo/Redo Operations
        //-------------------------------------------------------------------------------------------------------
        // Reverts the last step, including all sub-commands if a group
        system& Undo(void) noexcept
        {
            assert(!m_Done);
            if (m_UndoIndex == 0) return *this;
            m_UndoIndex--;

            auto& Entry   = *m_History[m_UndoIndex];
            auto  CmdName = getCommandName(Entry.m_CommandString);

            if (Entry.m_CacheUndoData.empty() && !m_UndoPath.empty())
            {
                job::warmup_cache   Job(*this, m_History[m_UndoIndex]);
                Job.Execute();
                assert(!Entry.m_CacheUndoData.empty());
            }

            {
                std::unique_lock<std::mutex> Lock(Entry.m_Mutex);
                undo_file                    File(Entry);
                if (Entry.IsGroup())
                {
                    // Undo all sub-commands in reverse order
                    for (auto it = Entry.m_SubCommands.rbegin(); it != Entry.m_SubCommands.rend(); ++it)
                    {
                        auto SubName  = getCommandName(it->m_CommandString);
                        auto SubCmdIt = m_Commands.find(std::string(SubName));

                        assert( SubCmdIt != m_Commands.end() );
                        if (SubCmdIt == m_Commands.end()) continue; // Skip invalid

                        auto& SubCmd = *SubCmdIt->second;

                        // Offset to the right place in the buffer
                        File.m_Index = it->m_DataOffset;

                        // Undo the sub-command
                        SubCmd.Undo(File);
                    }
                }
                else
                {
                    auto  CmdIt = m_Commands.find(std::string(CmdName));

                    // Invalid command, skip
                    assert(CmdIt != m_Commands.end());
                    if (CmdIt == m_Commands.end())
                        return *this;

                    // Undo the command
                    CmdIt->second->Undo(File);
                }
            }

            if (!m_UndoPath.empty())
            {
                m_LRU.push_back(m_History[m_UndoIndex]);
                UpdateLRU();
            }
            return *this;
        }

        // Reapplies the next step, including all sub-commands if a group
        system& Redo(void) noexcept
        {
            assert(!m_Done);
            if (m_UndoIndex >= m_History.size()) return *this;
            auto& Entry   = *m_History[m_UndoIndex];

            {
                std::unique_lock<std::mutex> Lock(Entry.m_Mutex);
                if (Entry.IsGroup())
                {
                    // Redo all sub-commands
                    for (const auto& SubCmd : Entry.m_SubCommands)
                    {
                        auto SubName = getCommandName(SubCmd.m_CommandString);
                        auto SubCmdIt = m_Commands.find(std::string(SubName));

                        assert(SubCmdIt != m_Commands.end());
                        if (SubCmdIt == m_Commands.end()) continue;

                        auto& Cmd = *SubCmdIt->second;
                        if (auto Err = Cmd.Parse(SubCmd.m_CommandString); !Err.empty())
                        {
                            assert(false);
                            continue;
                        }
                        if (auto Err = Cmd.Redo(); !Err.empty())
                        {
                            assert(false);
                            continue;
                        }
                    }
                }
                else
                {
                    auto  CmdName = getCommandName(Entry.m_CommandString);
                    auto  CmdIt   = m_Commands.find(std::string(CmdName));

                    // Invalid command, skip
                    if (CmdIt == m_Commands.end())
                        return *this;

                    if (auto Err = CmdIt->second->Parse(Entry.m_CommandString); !Err.empty()) return *this;
                    if (auto Err = CmdIt->second->Redo(); !Err.empty()) return *this;
                }
            }

            if (!m_UndoPath.empty())
            {
                m_LRU.push_back(m_History[m_UndoIndex]);
                UpdateLRU();
            }
            m_UndoIndex++;
            return *this;
        }

        //-------------------------------------------------------------------------------------------------------
        // History Display and Suggestion
        //-------------------------------------------------------------------------------------------------------
        // Displays the current history with sub-commands
        void displayHistory() const noexcept
        {
            std::cout << "History:\n";
            for (size_t i = 0; i < m_History.size(); ++i)
            {
                std::cout << std::format("  [{:04}]-[{}] User:{} Time:{} {} {}\n"
                    , i
                    , i < m_UndoIndex ? "U" : "R"
                    , m_History[i]->m_UserID
                    , m_History[i]->m_TimeStamp
                    , m_History[i]->m_CommandString
                    , m_History[i]->m_CacheUndoData.size() ? "[Cached]" : ""
                    );
                if (m_History[i]->IsGroup())
                {
                    for (const auto& SubCmd : m_History[i]->m_SubCommands)
                    {
                        std::cout << std::format("    - {}\n", SubCmd.m_CommandString);
                    }
                }
            }
            std::cout << "Current Index: " << m_UndoIndex << "\n";
        }

        // Suggests the next move based on the last command
        [[nodiscard]] std::string SuggestNext(int UserID) noexcept
        {
            if (m_UndoIndex == 0) return "-Move 0 0";
            auto& Last = *m_History[m_UndoIndex - 1];
            if (Last.m_UserID != UserID || Last.m_CommandString.find("Move") == std::string::npos) return "-Move 0 0";

            size_t Pos = Last.m_CommandString.find("-T");
            assert(Pos != std::string::npos);

            // Skip "-T "
            Pos += 3; 
            const size_t Space = Last.m_CommandString.find(' ', Pos);

            assert(Space != std::string::npos);
            int X = std::stoi(Last.m_CommandString.substr(Pos, Space - Pos));
            int Y = std::stoi(Last.m_CommandString.substr(Space + 1));
            return std::format("-Move -T {} {}", X + 10, Y + 10);
        }

        //-------------------------------------------------------------------------------------------------------
        // Utility Methods
        //-------------------------------------------------------------------------------------------------------
        // Returns the undo file storage path
        const std::string_view getUndoPath() const noexcept
        {
            return m_UndoPath;
        }

        // Saves history timestamps to disk
        [[nodiscard]] std::string SaveTimestamps(std::string_view FilePath = {}) noexcept
        {
            assert(!m_Done);
            assert(!m_UndoPath.empty());

            std::string Path;
            if (FilePath.empty())
            {
                Path = std::format("{}/UndoTimestamps.bin", m_UndoPath);
                FilePath = Path;
            }

            FILE* File;
            if (auto Err = fopen_s(&File, FilePath.data(), "wb"); Err)
            {
                char ErrMsg[100];
                strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                return std::format("Error saving timestamps: {}", ErrMsg);
            }
            uint32_t Count = static_cast<uint32_t>(m_UndoIndex);
            std::fwrite(&Count, sizeof(uint32_t), 1, File);
            for (uint32_t i = 0; i < Count; ++i)
            {
                const auto& Entry = *m_History[i];
                std::fwrite(&Entry.m_TimeStamp, sizeof(uint64_t), 1, File);
            }
            fclose(File);
            return {};
        }

        // Loads history timestamps from disk
        // Loads history timestamps from disk, newest steps up to max
        [[nodiscard]] std::string LoadTimestamps(std::string_view FilePath = {}) noexcept
        {
            assert(!m_Done);
            assert(!m_UndoPath.empty());

            std::string Path;
            if (FilePath.empty())
            {
                Path = std::format("{}/UndoTimestamps.bin", m_UndoPath);
                FilePath = Path;
            }

            SynJobQueue();
            m_History.clear();
            m_LRU.clear();
            m_UndoIndex = 0;

            FILE* File;
            if (auto Err = fopen_s(&File, FilePath.data(), "rb"); !Err)
            {
                uint32_t Count;
                std::fread(&Count, sizeof(uint32_t), 1, File);

                // Calculate steps to load and skip (newest at end)
                uint32_t LoadCount = std::min(Count, static_cast<uint32_t>(m_MaxUndoSteps));
                uint32_t SkipCount = Count > m_MaxUndoSteps ? Count - m_MaxUndoSteps : 0;

                // Skip oldest steps if over max
                if (SkipCount > 0)
                {
                    std::fseek(File, SkipCount * sizeof(uint64_t), SEEK_CUR);
                }

                m_History.resize(LoadCount);
                for (uint32_t i = 0; i < LoadCount; ++i)
                {
                    m_History[i] = std::make_shared<history_entry>();
                    std::fread(&m_History[i]->m_TimeStamp, sizeof(uint64_t), 1, File);
                    m_History[i]->m_bHasBeenSaved = true;
                    PushJob(std::make_unique<job::load_entries>(*this, m_History[i]));
                }
                fclose(File);
                m_UndoIndex = LoadCount;
            }
            else
            {
                char ErrMsg[100];
                strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                return std::format("Error: {}", ErrMsg);
            }

            SynJobQueue();
            for (int i = std::max(0, static_cast<int>(m_UndoIndex) - static_cast<int>(m_MaxCachedSteps)); i < m_UndoIndex; ++i)
            {
                m_LRU.push_back(m_History[i]);
                if (m_History[i]->m_CacheUndoData.empty() && m_History[i]->m_bHasBeenSaved)
                    PushJob(std::make_unique<job::warmup_cache>(*this, m_History[i]));
            }

            // let the user sync if he wants to... 
            // SynJobQueue();

            return {};
        }
    protected:
        //-------------------------------------------------------------------------------------------------------
        // Internal Helper Methods
        //-------------------------------------------------------------------------------------------------------
        // Registers a command with the system
        void RegisterCommand(command_base& Cmd, std::string_view Name) noexcept
        {
            m_Commands[std::string(Name)] = &Cmd;
            Cmd.m_hHelp = Cmd.m_Parser.addOption("h", "Show this help message\nUse -h or --h to display", false, 0);
        }

        // Manages LRU cache for recent steps
        void UpdateLRU() noexcept
        {
            if (m_History.empty()) return;
            assert(m_MaxCachedSteps > (m_LookAheadSteps * 2 + 1));
            const auto SizeEstimation = m_MaxCachedSteps - m_LookAheadSteps * 2 - 1;
            while (m_LRU.size() > SizeEstimation)
            {
                auto Oldest = m_LRU.front();
                std::unique_lock<std::mutex> Lock(Oldest->m_Mutex);
                if (Oldest->m_bHasBeenSaved) Oldest->m_CacheUndoData.clear();
                Lock.unlock();
                m_LRU.pop_front();
            }

            for (int i = 1; i <= m_LookAheadSteps && m_LRU.size() < m_MaxCachedSteps; ++i)
            {
                if (m_UndoIndex >= i && m_History[m_UndoIndex - i]->m_CacheUndoData.empty())
                {
                    PushJob(std::make_unique<job::warmup_cache>(*this, m_History[m_UndoIndex - i]));
                    m_LRU.push_back(m_History[m_UndoIndex - i]);
                }
                if (m_UndoIndex + i < m_History.size() && m_History[m_UndoIndex + i]->m_CacheUndoData.empty())
                {
                    PushJob(std::make_unique<job::warmup_cache>(*this, m_History[m_UndoIndex + i]));
                    m_LRU.push_back(m_History[m_UndoIndex + i]);
                }
            }
        }

        // Queues an I/O job for execution
        void PushJob(std::unique_ptr<job::base>&& Job) noexcept
        {
            {
                std::lock_guard<std::mutex> Lock(m_Mutex);
                m_IOQueue.push(std::move(Job));
            }
            m_Cond.notify_one();
        }

        // Synchronizes the I/O job queue
        void SynJobQueue() noexcept
        {
            if (m_UndoPath.empty()) return;
            std::unique_lock<std::mutex> Lock(m_Mutex);
            while (!m_Cond.wait_for(Lock, std::chrono::milliseconds(100), [this] { return m_IOQueue.empty(); }))
            {
            }
        }

        // Prunes future history if inserting mid-stack
        void PruneHistory() noexcept
        {
            if (m_UndoIndex >= m_History.size()) return;
            std::vector<std::uint64_t> TimeStamps;
            TimeStamps.reserve(m_History.size() - m_UndoIndex);

            for (auto i = m_UndoIndex; i < m_History.size(); ++i)
            {
                TimeStamps.push_back(m_History[i]->m_TimeStamp);
            }

            if (m_UndoPath.empty())
            {
                for (auto& TimeStamp : TimeStamps)
                {
                    std::filesystem::remove(std::format("{}/UndoStep-{}", m_UndoPath, TimeStamp));
                }
            }
            else
            {
                PushJob(std::make_unique<job::delete_entries>(*this, std::move(TimeStamps)));
            }
            m_History.resize(m_UndoIndex);
        }

        // Prunes oldest steps if history exceeds max size
        void PruneOldSteps() noexcept
        {
            // No pruning needed if under or at max
            if (m_History.size() <= m_MaxUndoSteps) return;

            std::vector<std::uint64_t> TimeStamps;

            // Excess only if size exceeds max—no underflow
            size_t Excess = m_History.size() > m_MaxUndoSteps ? m_History.size() - m_MaxUndoSteps : 0;
            TimeStamps.reserve(Excess);

            // Collect timestamps of oldest steps to delete
            for (size_t i = 0; i < Excess; ++i)
            {
                TimeStamps.push_back(m_History[i]->m_TimeStamp);
            }

            // Remove oldest steps from history first
            m_History.erase(m_History.begin(), m_History.begin() + Excess);
            m_UndoIndex -= static_cast<int>(Excess);

            // Safety: keep index valid
            if (m_UndoIndex < 0) m_UndoIndex = 0; 

            // Prune LRU entries matching pruned timestamps
            m_LRU.remove_if([&TimeStamps](const std::shared_ptr<history_entry>& Entry) 
            {
                return std::find(TimeStamps.begin(), TimeStamps.end(), Entry->m_TimeStamp) != TimeStamps.end();
            });

            // Delete files only if saved to disk and path exists
            if (!TimeStamps.empty() && !m_UndoPath.empty())
            {
                PushJob(std::make_unique<job::delete_entries>(*this, std::move(TimeStamps)));
            }
        }

        // Worker thread for processing I/O jobs
        static void IOWorker(system& System) noexcept
        {
            while (true)
            {
                std::unique_lock<std::mutex> Lock(System.m_Mutex);
                std::unique_ptr<job::base> Job;
                System.m_Cond.wait(Lock, [&System] { return !System.m_IOQueue.empty() || System.m_Done; });
                if (System.m_Done && System.m_IOQueue.empty()) return;
                if (!System.m_IOQueue.empty())
                {
                    Job = std::move(System.m_IOQueue.front());
                    System.m_IOQueue.pop();
                }
                else continue;
                Lock.unlock();
                Job->Execute();
            }
        }

        //-------------------------------------------------------------------------------------------------------
        // Members: Core system state
        //-------------------------------------------------------------------------------------------------------
        using history_vector = std::vector<std::shared_ptr<history_entry>>;
        using lru_list       = std::list<std::shared_ptr<history_entry>>;
        using command_map    = std::unordered_map<std::string, command_base*>;
        using io_queue       = std::queue<std::unique_ptr<job::base>>;

        int                                 m_UndoIndex         = 0;
        history_vector                      m_History           = {};
        lru_list                            m_LRU               = {};
        command_map                         m_Commands          = {};
        std::string                         m_UndoPath          = {};
        int                                 m_DefaultUser       = 1;
        size_t                              m_MaxCachedSteps    = 50;
        size_t                              m_LookAheadSteps    = 5;
        std::vector<std::thread>            m_IOThread          = {};
        mutable std::mutex                  m_Mutex             = {};
        std::condition_variable             m_Cond              = {};
        io_queue                            m_IOQueue           = {};
        uint32_t                            m_MaxUndoSteps      = 1000; // Max undo steps limit
        bool                                m_Done              = true;
        bool                                m_bAutoLoadSave     = false;
        std::uint64_t                       m_CommandCounter    = 0;
        friend int system_example::StressTest();
        friend struct command_base;
        friend class history;
    };

    //-----------------------------------------------------------------------------------------------------------
    // Command Base Implementation
    //-----------------------------------------------------------------------------------------------------------
    command_base::command_base(system& System, const char* pName, void* pDataBase) noexcept
        : m_System(System), m_pCommandName(pName), m_pDataBase(pDataBase)
    {
        m_System.RegisterCommand(*this, pName);
    }

    //-----------------------------------------------------------------------------------------------------------

    void job::save_to_disk::Execute() noexcept
    {
        std::unique_lock<std::mutex> Lock(m_Entry->m_Mutex);
        if (!m_Entry->m_bHasBeenSaved && Save(*m_Entry, m_System.getUndoPath()))
            m_Entry->m_bHasBeenSaved = true;
    }

    //-----------------------------------------------------------------------------------------------------------

    void job::delete_entries::Execute() noexcept
    {
        for (auto& TimeStamp : m_TimeStamps)
            std::filesystem::remove(std::format("{}/UndoStep-{}", m_System.getUndoPath(), TimeStamp));
    }

    //-----------------------------------------------------------------------------------------------------------

    void job::warmup_cache::Execute() noexcept
    {
        std::unique_lock<std::mutex> Lock(m_Entry->m_Mutex);
        if (m_Entry->m_CacheUndoData.empty())
            Load(*m_Entry, m_System.getUndoPath(), false, true);
    }

    //-----------------------------------------------------------------------------------------------------------

    void job::load_entries::Execute() noexcept
    {
        std::unique_lock<std::mutex> Lock(m_Entry->m_Mutex);
        warmup_cache::Load(*m_Entry, m_System.getUndoPath(), true, false);
    }
}
#endif // XUNDO_H