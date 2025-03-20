#ifndef XUNDO_H
#define XUNDO_H
#pragma once

#include "../dependencies/xcmdline/source/xcmdline_parser.h"
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

//
// Dependencies
//
namespace xundo
{
    class system;
    struct command_base;
    namespace example{ int StressTest(); }
}

//
// Undo System
//
namespace xundo
{
    // This structure holds the history of commands
    struct history_entry
    {
        mutable std::mutex      m_Mutex;                 // Mutex to protect the cache
        int                     m_UserID;                // User ID  
        std::uint64_t           m_TimeStamp;             // Time stamp
        std::string             m_CommandString;         // Command string
        std::vector<std::byte>  m_CacheUndoData;         // Cache undo data
        bool                    m_bHasBeenSaved = false; // Has this entry been saved to disk
    };

    // This class is used to read and write data to the undo cache
    struct undo_file
    {
        history_entry&      m_Entry;    // Reference to the history entry
        std::uint32_t       m_Index;    // Current index

        undo_file(history_entry& Entry, std::uint32_t Index = 0) :m_Entry(Entry), m_Index(Index)
        {
        }

        void Write(const void* pData, std::uint64_t Size) noexcept
        {
            auto& Cache = m_Entry.m_CacheUndoData;
            assert(pData);
            Cache.insert(Cache.begin() + m_Index, reinterpret_cast<const std::byte*>(pData), reinterpret_cast<const std::byte*>(pData) + Size);
            m_Index += static_cast<std::uint32_t>(Size);
        }

        template<typename T>
        void Write(const T& Data) noexcept
        {
            Write(&Data, sizeof(T));
        }

        void Read(void* pData, std::uint64_t Size) noexcept
        {
            auto& Cache = m_Entry.m_CacheUndoData;
            assert(pData && m_Index + Size <= Cache.size());
            std::memcpy(pData, Cache.data() + m_Index, Size);
            m_Index += static_cast<std::uint32_t>(Size);
        }

        template<typename T>
        void Read(T& Data) noexcept
        {
            Read(&Data, sizeof(T));
        }
    };

    // This namespace contains the jobs that are executed by the IO worker
    namespace job
    {
        // Base class for all jobs
        struct base
        {
            virtual            ~base() = default;
            virtual void        Execute() noexcept = 0;
        };

        // This job saves the history entry to disk
        struct save_to_disk final : base
        {
            save_to_disk(system& System, std::shared_ptr<history_entry> Entry) noexcept
                : m_System(System), m_Entry(Entry)
            {
            }

            void Execute() noexcept override;

            static bool Save(const history_entry& Entry, std::string_view Path) noexcept
            {
                FILE* File;
                if (auto Err = fopen_s(&File, std::format("{}/UndoStep-{}", Path, Entry.m_TimeStamp).c_str(), "wb"); Err)
                {
                    char ErrMsg[100];
                    strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                    std::printf("Error: %s\n", ErrMsg);
                    return false;
                }
                bool Ok = true;

                uint32_t DataLen = static_cast<uint32_t>(Entry.m_CacheUndoData.size());
                Ok &= fwrite(&DataLen, sizeof(uint32_t), 1, File) == 1;
                Ok &= fwrite(Entry.m_CacheUndoData.data(), DataLen, 1, File) == 1;
                Ok &= fwrite(&Entry.m_UserID, sizeof(int), 1, File) == 1;
                Ok &= fwrite(&Entry.m_TimeStamp, sizeof(uint64_t), 1, File) == 1;

                uint32_t StrLen = static_cast<uint32_t>(Entry.m_CommandString.size());
                Ok &= fwrite(&StrLen, sizeof(uint32_t), 1, File) == 1;
                Ok &= fwrite(Entry.m_CommandString.data(), StrLen, 1, File) == 1;
                fclose(File);
                return Ok;
            }
            system&                         m_System;
            std::shared_ptr<history_entry>  m_Entry;
        };

        // This job deletes the history entries from disk
        struct delete_entries final : base
        {
            delete_entries(system& System, std::vector<std::uint64_t>&& TimeStamps) noexcept
                : m_System(System), m_TimeStamps(std::move(TimeStamps))
            {
            }

            void Execute() noexcept override;

            system&                     m_System;
            std::vector<std::uint64_t>  m_TimeStamps;
        };

        // This job loads the history entry from disk
        struct warmup_cache final : base
        {
            warmup_cache(system& System, std::shared_ptr<history_entry> Entry) noexcept
                : m_System(System), m_Entry(Entry)
            {
            }

            void Execute() noexcept override;

            static bool Load(history_entry& Entry, std::string_view Path, bool bLoadKeyData, bool bLoadCacheData) noexcept
            {
                FILE* File;
                if (auto Err = fopen_s(&File, std::format("{}/UndoStep-{}", Path, Entry.m_TimeStamp).c_str(), "rb"); Err)
                {
                    char ErrMsg[100];
                    strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                    std::printf("Error: %s\n", ErrMsg);
                    return false;
                }

                bool Ok = true;
                uint32_t DataLen;
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
                    uint32_t StrLen;
                    Ok &= fread(&StrLen, sizeof(uint32_t), 1, File) == 1;
                    Entry.m_CommandString.resize(StrLen);
                    Ok &= fread(Entry.m_CommandString.data(), StrLen, 1, File) == 1;
                }

                fclose(File);
                return Ok;
            }

            system&                         m_System;
            std::shared_ptr<history_entry>  m_Entry;
        };

        // This job deletes the history entries from disk
        struct load_entries final : base
        {
            load_entries(system& System, std::shared_ptr<history_entry> Entry) : m_System(System), m_Entry(Entry)
            {
            }

            void Execute() noexcept override;

            system&                         m_System;
            std::shared_ptr<history_entry>  m_Entry;
        };

    };

    // This is the base class for all commands
    struct command_base
    {
        command_base(system& System, const char* pName, void* pDataBase) noexcept;

        template<typename T>T& get() noexcept
        {
            return *static_cast<T*>(m_pDataBase);
        }

        virtual                        ~command_base        (void)                          noexcept = default;
        virtual const char*             getCommandHelp      (void)                  const   noexcept = 0;
        virtual void                    RegisterArguments   (void)                          noexcept = 0;
        virtual std::string             Redo                (void)                          noexcept = 0;
        virtual void                    Undo                (undo_file& File)               noexcept = 0;
        virtual void                    BackupCurrenState   (undo_file& File)               noexcept = 0;
        std::string                     Parse               (std::string_view cmd_str)      noexcept
        {
            m_Parser.clearArgs();
            return m_Parser.Parse(cmd_str);
        }

        system&                         m_System;
        xcmdline::parser                m_Parser        = {};
        const char*                     m_pCommandName  = {};
        void*                           m_pDataBase     = {};
        xcmdline::parser::handle        m_hHelp         = {};
    };

    // This function extracts the command name from a string
    std::string_view getCommandName(std::string_view str)
    {
        size_t pos = str.find(' ');
        return pos == std::string_view::npos ? str : str.substr(0, pos);
    }

    // This is the main class that manages the undo system
    class system
    {
    public:

        system() = default;
        ~system() noexcept
        {
            //
            // Make sure we save the timestamps before we exit
            //

            // Prune to only active steps
            if (!m_UndoPath.empty())
            {
                if (m_bAutoLoadSave)
                {
                    if (auto Err = SaveTimestamps(); !Err.empty())
                    {
                        std::cerr << Err << "\n";
                    }
                }
            }

            //
            // Signal the IO threads to exit
            //
            if (m_Done==false)
            {
                // Set done to true...
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Done = true;
                }
                m_Cond.notify_all();

                // wait for the threats to exit
                for (auto& E : m_IOThread)
                {
                    E.join();
                }
            }
        }

        [[nodiscard]] std::string Init( std::string_view UndoPath = {}, bool bAutoLoadSave = true ) noexcept
        {
            m_UndoPath          = UndoPath;
            m_bAutoLoadSave     = bAutoLoadSave;
            m_Done              = false;

            if (!UndoPath.empty())
            {
                for (int i = 0; i < 4; ++i) m_IOThread.emplace_back(std::thread(&system::IOWorker, std::ref(*this)));

                if (m_bAutoLoadSave)
                {
                    if (std::string Path = std::format("{}/UndoTimestamps.bin", m_UndoPath); std::filesystem::exists(Path) )
                    {
                        return LoadTimestamps(Path);
                    }
                }
            }
            else
            {
                assert( m_bAutoLoadSave == false );
            }

            return {};
        }

        [[nodiscard]] std::string Execute(std::string_view cmd_str, int UserID = -1)
        {
            assert(m_Done==false);

            auto name = getCommandName(cmd_str);
            auto Cmd  = m_Commands.find(std::string(name));
            
            if(Cmd != m_Commands.end())
            {
                return Execute( *Cmd->second, cmd_str, UserID);
            }

            return std::format("Unable find the command: {}", name);
        }

        [[nodiscard]] std::string Execute(command_base& Cmd, std::string_view cmd_str, int UserID = -1) noexcept
        {
            assert(m_Done == false);

            // Check to see if the command line has any errors
            if (auto err = Cmd.Parse(cmd_str); !err.empty()) return err;

            // Check for help flag
            if (Cmd.m_Parser.hasOption(Cmd.m_hHelp))
            {
                Cmd.m_Parser.printHelp();
                return {};
            }

            // Ready to begin execution...
            auto Entry = std::make_shared<history_entry>();
            if (UserID == -1)UserID = m_DefaultUser;

            Entry->m_UserID         = UserID;
            Entry->m_TimeStamp      = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() * 1000 + m_CommandCounter++;
            Entry->m_CommandString  = cmd_str;
            {
                undo_file File(*Entry);
                Cmd.BackupCurrenState(File);
            }

            if (auto Err = Cmd.Redo(); !Err.empty()) return Err;

            PruneHistory();
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

        system& Undo(void) noexcept
        {
            assert(m_Done == false);

            if (m_UndoIndex == 0) return *this;
            m_UndoIndex--;

            auto&   LastCommand = *m_History[m_UndoIndex];
            auto    CmdName     = getCommandName(LastCommand.m_CommandString);
            auto&   Cmd         = *m_Commands[std::string(CmdName)];

            // Force a sync if we need to
            if (LastCommand.m_CacheUndoData.empty())
            {
                assert(!m_UndoPath.empty());
                job::warmup_cache Job(*this, m_History[m_UndoIndex]);
                Job.Execute();
                assert( LastCommand.m_CacheUndoData.empty() == false );
            }

            {
                std::unique_lock<std::mutex> lock(LastCommand.m_Mutex);
                undo_file File(LastCommand);
                Cmd.Undo(File);
            }

            if (!m_UndoPath.empty())
            {
                m_LRU.push_back(m_History[m_UndoIndex]);
                UpdateLRU();
            }
            return *this;
        }

        system& Redo(void) noexcept
        {
            assert(m_Done == false);

            if (m_UndoIndex >= m_History.size())return *this;
            auto& LastCommand   = *m_History[m_UndoIndex];
            auto  CmdName       = getCommandName(LastCommand.m_CommandString);
            auto& Cmd           = *m_Commands[std::string(CmdName)];

            {
                std::unique_lock<std::mutex> lock(LastCommand.m_Mutex);

                // We really should not have any errors here since the command was executed one time already
                if (auto Err = Cmd.Parse(LastCommand.m_CommandString); !Err.empty()) return *this;
                if (auto Err = Cmd.Redo(); !Err.empty()) return *this;
            }

            if (!m_UndoPath.empty())
            {
                m_LRU.push_back(m_History[m_UndoIndex]);
                UpdateLRU();
            }
            m_UndoIndex++;

            return *this;
        }

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
            }
            std::cout << "Current Index: " << m_UndoIndex << "\n";
        }

        [[nodiscard]] std::string SuggestNext(int UserID) noexcept
        {
            if (m_UndoIndex == 0)return "-Move 0 0";
            auto& last = *m_History[m_UndoIndex - 1];
            if (last.m_UserID != UserID || last.m_CommandString.find("Move") == std::string::npos)return "-Move 0 0";

            size_t pos = last.m_CommandString.find("-T");
            assert(pos != std::string::npos);
            pos += 3; // Skip "-T "
            size_t space = last.m_CommandString.find(' ', pos);
            assert(space != std::string::npos);
            int X = std::stoi(last.m_CommandString.substr(pos, space - pos));
            int Y = std::stoi(last.m_CommandString.substr(space + 1));
            return std::format("-Move -T {} {}", X + 10, Y + 10);
        }

        const std::string_view getUndoPath() const noexcept
        {
            return m_UndoPath;
        }

        // Saves history timestamps to disk
        [[nodiscard]] std::string SaveTimestamps(std::string_view FilePath={}) noexcept
        {
            assert(m_Done == false);
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
            for (uint32_t i=0; i< Count; ++i)
            {
                const auto& Entry = *m_History[i];
                std::fwrite(&Entry.m_TimeStamp, sizeof(uint64_t), 1, File);
            }
            fclose(File);

            return {};
        }

        // Loads history timestamps from disk
        [[nodiscard]] std::string LoadTimestamps( std::string_view FilePath={} ) noexcept
        {
            assert(m_Done == false);
            assert(!m_UndoPath.empty());

            std::string Path;
            if (FilePath.empty())
            {
                Path = std::format("{}/UndoTimestamps.bin", m_UndoPath);
                FilePath = Path;
            }

            //
            // Make sure everything is reset to zero
            //

            // Wait for all load_entries jobs to finish
            SynJobQueue();

            m_History.clear();
            m_LRU.clear();
            m_UndoIndex = 0;

            //
            // Load history from saved timestamps
            //
            FILE* File;
            if (auto Err = fopen_s(&File, FilePath.data(), "rb"); !Err)
            {
                uint32_t Count;
                std::fread(&Count, sizeof(uint32_t), 1, File);
                m_History.resize(Count);
                for (uint32_t i = 0; i < Count; ++i)
                {
                    m_History[i] = std::make_shared<history_entry>();
                    uint64_t TimeStamp;
                    std::fread(&TimeStamp, sizeof(uint64_t), 1, File);
                    m_History[i]->m_TimeStamp = TimeStamp;
                    m_History[i]->m_bHasBeenSaved = true;
                    PushJob(std::make_unique<job::load_entries>(*this, m_History[i]));
                }
                fclose(File);
                m_UndoIndex = Count;
            }
            else
            {
                char ErrMsg[100];
                strerror_s(ErrMsg, sizeof(ErrMsg), Err);
                return std::format("Error: {}", ErrMsg);
            }

            // Wait for all load_entries jobs to finish
            SynJobQueue();

            //
            // Cache latest steps
            //
            for (int i = std::max(0, static_cast<int>(m_UndoIndex) - static_cast<int>(m_MaxCachedSteps)); i < m_UndoIndex; ++i)
            {
                m_LRU.push_back(m_History[i]);
                if (m_History[i]->m_CacheUndoData.empty() && m_History[i]->m_bHasBeenSaved)
                    PushJob(std::make_unique<job::warmup_cache>(*this, m_History[i]));
            }

            return {};
        }

    protected:

        void RegisterCommand(command_base& Cmd, std::string_view Name) noexcept
        {
            m_Commands[std::string(Name)] = &Cmd;
            Cmd.m_hHelp = Cmd.m_Parser.addOption("h", "Show this help message\nUse -h or --h to display", false, 0);
        }

        void UpdateLRU() noexcept
        {
            if (m_History.empty())return;

            assert(m_MaxCachedSteps > (m_LookAheadSteps*2+1) );
            const auto SizeEstimation = m_MaxCachedSteps - m_LookAheadSteps * 2 - 1;
            while (m_LRU.size() > SizeEstimation)
            {
                auto Oldest = m_LRU.front();
                std::unique_lock<std::mutex> lock(Oldest->m_Mutex);
                if (Oldest->m_bHasBeenSaved) Oldest->m_CacheUndoData.clear();
                lock.unlock();
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

        void PushJob(std::unique_ptr<job::base>&& Job) noexcept
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_IOQueue.push(std::move(Job));
            m_Cond.notify_one();
        }

        void SynJobQueue() noexcept
        {
            if (m_UndoPath.empty()) return;
            std::unique_lock<std::mutex> Lock(m_Mutex);
            while (!m_Cond.wait_for(Lock, std::chrono::milliseconds(100), [this] {return m_IOQueue.empty(); }))
            {
            }
        }

        // If we are in the middle of the undo buffer and we execute a new command, we need to prune the history
        void PruneHistory() noexcept
        {
            if (m_UndoIndex >= m_History.size())return;
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

        // This is the worker thread that handles IO operations
        static void IOWorker(system& System) noexcept
        {
            while (true)
            {
                std::unique_ptr<job::base> Job;
                {
                    std::unique_lock<std::mutex> lock(System.m_Mutex);
                    System.m_Cond.wait(lock, [&System] {return !System.m_IOQueue.empty() || System.m_Done; });
                    if (System.m_Done && System.m_IOQueue.empty())return;
                    if (!System.m_IOQueue.empty())
                    {
                        Job = std::move(System.m_IOQueue.front());
                        System.m_IOQueue.pop();
                    }
                    else continue;
                }
                Job->Execute();
            }
        }

    protected:

        int                                             m_UndoIndex         = 0;
        std::vector<std::shared_ptr<history_entry>>     m_History           = {};
        std::list<std::shared_ptr<history_entry>>       m_LRU               = {};
        std::unordered_map<std::string, command_base*>  m_Commands          = {};
        std::string                                     m_UndoPath          = {};
        int                                             m_DefaultUser       = 1;
        size_t                                          m_MaxCachedSteps    = 50;
        size_t                                          m_LookAheadSteps    = 5;
        std::vector<std::thread>                        m_IOThread          = {};
        mutable std::mutex                              m_Mutex             = {};
        std::condition_variable                         m_Cond              = {};
        std::queue<std::unique_ptr<job::base>>          m_IOQueue           = {};
        bool                                            m_Done              = true;
        bool                                            m_bAutoLoadSave     = false;
        std::uint64_t                                   m_CommandCounter    = 0;

    protected:

        friend int example::StressTest();
        friend struct command_base;
    };

    //-----------------------------------------------------------------------------------------------------------
    // Implementation of the command_base class
    //-----------------------------------------------------------------------------------------------------------
    inline
    command_base::command_base(system& System, const char* pName, void* pDataBase) noexcept
        : m_System(System), m_pCommandName(pName), m_pDataBase(pDataBase)
    {
        m_System.RegisterCommand(*this, pName);
    }

    //-----------------------------------------------------------------------------------------------------------
    //-----------------------------------------------------------------------------------------------------------
    namespace job
    {
        //-----------------------------------------------------------------------------------------------------------
        inline
        void save_to_disk::Execute() noexcept
        {
            std::unique_lock<std::mutex> lock(m_Entry->m_Mutex);
            if (!m_Entry->m_bHasBeenSaved && Save(*m_Entry, m_System.getUndoPath()))
                m_Entry->m_bHasBeenSaved = true;
        }

        //-----------------------------------------------------------------------------------------------------------
        inline
        void delete_entries::Execute() noexcept
        {
            for (auto& TimeStamp : m_TimeStamps)
                std::filesystem::remove(std::format("{}/UndoStep-{}", m_System.getUndoPath(), TimeStamp));
        }

        //-----------------------------------------------------------------------------------------------------------
        inline
        void warmup_cache::Execute() noexcept
        {
            std::unique_lock<std::mutex> lock(m_Entry->m_Mutex);
            if (m_Entry->m_CacheUndoData.empty())
                Load(*m_Entry, m_System.getUndoPath(), false, true );
        }

        //-----------------------------------------------------------------------------------------------------------

        inline
        void load_entries::Execute() noexcept
        {
            std::unique_lock<std::mutex> lock(m_Entry->m_Mutex);
            warmup_cache::Load(*m_Entry, m_System.getUndoPath(), true, false );
        }
    }
}
#endif // XUNDO_H