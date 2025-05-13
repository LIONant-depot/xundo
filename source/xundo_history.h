#ifndef XUNDO_HISTORY_H
#define XUNDO_HISTORY_H
#pragma once

#include "xundo_system.h"

namespace xundo
{
    //-----------------------------------------------------------------------------------------------------------
    // History: Manages multiple undo systems with unified history
    //-----------------------------------------------------------------------------------------------------------
    class history
    {
    public:
        //-------------------------------------------------------------------------------------------------------
        // Construction/Destruction
        //-------------------------------------------------------------------------------------------------------
        history() = default;

        // Cleans up all managed systems
        ~history() noexcept
        {
            for (auto& Sys : m_Systems)
            {
                if (Sys.second.m_bOwnMemory) delete Sys.second.m_System;
            }
        }

        //-------------------------------------------------------------------------------------------------------
        // System Management
        //-------------------------------------------------------------------------------------------------------
        // Adds a new undo system with GUID
        void AddSystem(const std::string& Name, uint64_t SystemGUID, system& Sys, bool bDeleteOnExit = false ) noexcept
        {
            assert(m_Systems.find(SystemGUID) == m_Systems.end());

            system_info Info;
            Info.m_SystemName   = Name;
            Info.m_System       = &Sys;
            Info.m_bOwnMemory   = bDeleteOnExit;
            m_Systems[SystemGUID] = Info;
        }

        // Gets a system by GUID
        system& GetSystem(uint64_t GUID) noexcept
        {
            auto It = m_Systems.find(GUID);
            assert(It != m_Systems.end());
            return *It->second.m_System;
        }

        //-------------------------------------------------------------------------------------------------------
        // Undo/Redo Operations
        //-------------------------------------------------------------------------------------------------------
        // Undoes the latest step across all systems
        void Undo() noexcept
        {
            if (m_GlobalHistory.empty() || m_UndoIndex <= 0) return;

            m_UndoIndex--;
            const auto& Entry = m_GlobalHistory[m_UndoIndex];
            system& MainSys = GetSystem(Entry.m_SystemGUID);
            assert(MainSys.m_UndoIndex - 1 == Entry.m_UndoIndex); // Check snapshot matches
            MainSys.Undo();
        }

        // Redoes the next step across all systems
        void Redo() noexcept
        {
            if (m_UndoIndex >= static_cast<int>(m_GlobalHistory.size())) return;

            const auto& Entry = m_GlobalHistory[m_UndoIndex];
            system& MainSys = GetSystem(Entry.m_SystemGUID);
            assert(MainSys.m_UndoIndex == Entry.m_UndoIndex); // Check snapshot matches
            MainSys.Redo();
            m_UndoIndex++;
        }

        // Undoes a step in a specific system, syncs global history
        void UndoSystem(uint64_t SystemGUID) noexcept
        {
            system& Sys = GetSystem(SystemGUID);
            if (Sys.m_UndoIndex <= 0) return;

            // Find last step for this system
            for (auto It = m_GlobalHistory.rbegin(); It != m_GlobalHistory.rend(); ++It)
            {
                if (It->m_SystemGUID == SystemGUID)
                {
                    // Multi-system step—stop unless only this system
                    if (!It->m_AffectedSystems.empty()) return;

                    Sys.Undo();
                    m_GlobalHistory.erase((It + 1).base());
                    m_UndoIndex--;
                    if (m_UndoIndex < 0) m_UndoIndex = 0;
                    break;
                }
            }
        }

        // Redoes a step in a specific system, recreates global entry
        void RedoSystem(uint64_t SystemGUID) noexcept
        {
            system& Sys = GetSystem(SystemGUID);
            if (Sys.m_UndoIndex >= static_cast<int>(Sys.m_History.size())) return;

            // Redo the local step
            Sys.Redo();

            // Recreate global history entry from local system history
            const auto& LocalEntry = *Sys.m_History[Sys.m_UndoIndex - 1];

            history_entry Entry;
            Entry.m_SystemGUID = SystemGUID;
            Entry.m_UndoIndex = Sys.m_UndoIndex - 1; // Match local index
            Entry.m_AffectedSystems = {}; // Default empty—parse if needed

            // Binary search to find insertion point based on timestamp
            size_t Left = 0;
            size_t Right = m_GlobalHistory.size();
            while (Left < Right)
            {
                size_t Mid = Left + (Right - Left) / 2;
                const auto& MidEntry = m_GlobalHistory[Mid];
                uint64_t MidTimeStamp = m_Systems[MidEntry.m_SystemGUID].m_System->m_History[MidEntry.m_UndoIndex]->m_TimeStamp;
                if (LocalEntry.m_TimeStamp < MidTimeStamp)
                {
                    Right = Mid;
                }
                else
                {
                    Left = Mid + 1;
                }
            }

            // Insert at found position (Left) or append if at end
            m_GlobalHistory.insert(m_GlobalHistory.begin() + Left, Entry);
            m_UndoIndex++;
        }

        //-------------------------------------------------------------------------------------------------------
        // Execution with History Tracking
        //-------------------------------------------------------------------------------------------------------
        // Executes a command on a specific system, logs side-effected systems
        [[nodiscard]] std::string Execute(uint64_t SystemGUID, std::string_view CmdStr, int UserID = -1, std::span< const uint64_t> AffectedSystems = {})
        {
            system& Sys = GetSystem(SystemGUID);

            std::string Result = Sys.Execute(CmdStr, UserID);
            if (Result.empty()) // Success—log it
            {
                if (m_UndoIndex < static_cast<int>(m_GlobalHistory.size()))
                {
                    m_GlobalHistory.resize(m_UndoIndex); // Prune future steps on new execute
                }

                history_entry Entry;
                Entry.m_SystemGUID = SystemGUID;
                Entry.m_UndoIndex = Sys.m_UndoIndex - 1; // Last executed step
                Entry.m_AffectedSystems.insert(Entry.m_AffectedSystems.begin(), AffectedSystems.begin(), AffectedSystems.end()); // Side effects only
                m_GlobalHistory.push_back(std::move(Entry));
                m_UndoIndex++;
            }
            return Result;
        }

        // Executes a group command on a specific system, logs side-effected systems
        [[nodiscard]] std::string Execute(uint64_t SystemGUID, std::string_view GroupName,
            const std::vector<std::string>& Cmds, int UserID = -1,
            std::span< const uint64_t> AffectedSystems = {}) noexcept
        {
            system& Sys = GetSystem(SystemGUID);

            std::string Result = Sys.Execute(GroupName, Cmds, UserID);
            if (Result.empty()) // Success—log it
            {
                if (m_UndoIndex < static_cast<int>(m_GlobalHistory.size()))
                {
                    m_GlobalHistory.resize(m_UndoIndex); // Prune future steps on new execute
                }

                history_entry Entry;
                Entry.m_SystemGUID = SystemGUID;
                Entry.m_UndoIndex = Sys.m_UndoIndex - 1; // Last executed step
                Entry.m_AffectedSystems.insert(Entry.m_AffectedSystems.begin(), AffectedSystems.begin(), AffectedSystems.end()); // Side effects only
                m_GlobalHistory.push_back(std::move(Entry));
                m_UndoIndex++;
            }
            return Result;
        }

        //-------------------------------------------------------------------------------------------------------
        // History Display
        //-------------------------------------------------------------------------------------------------------
        // Displays the global history across all systems
        void DisplayHistory() const noexcept
        {
            std::cout << "Global History:\n";
            for (size_t i = 0; i < m_GlobalHistory.size(); ++i)
            {
                const auto& Entry = m_GlobalHistory[i];
                const auto& SystemInfo = m_Systems.at(Entry.m_SystemGUID);

                std::cout << std::format("  [{:04}] System:{} Time:{} Cmd:{}\n",
                    i,
                    SystemInfo.m_SystemName,
                    SystemInfo.m_System->m_History[Entry.m_UndoIndex]->m_TimeStamp,
                    SystemInfo.m_System->m_History[Entry.m_UndoIndex]->m_CommandString);
            }
            std::cout << "Current Index: " << m_UndoIndex << "\n";
        }

    private:
        //-------------------------------------------------------------------------------------------------------
        // History Entry: Tracks a single action across systems
        //-------------------------------------------------------------------------------------------------------
        struct history_entry
        {
            uint64_t                m_SystemGUID;       // GUID of the system executing the action
            int                     m_UndoIndex;        // Undo index from the system command
            std::vector<uint64_t>   m_AffectedSystems;  // Side-effected systems (excludes main system)
        };

        //-------------------------------------------------------------------------------------------------------
        // System Info: Stores system pointer and its name
        //-------------------------------------------------------------------------------------------------------
        struct system_info
        {
            std::string     m_SystemName    = {};       // Name of the system
            system*         m_System        = nullptr;  // Pointer to the system
            bool            m_bOwnMemory    = false;    // Tells if we own the memory or not... (If we own it we need to free it)
        };

        //-------------------------------------------------------------------------------------------------------
        // Members
        //-------------------------------------------------------------------------------------------------------
        std::unordered_map<uint64_t, system_info> m_Systems;        // Undo systems keyed by GUID
        std::vector<history_entry>                m_GlobalHistory;  // Global timeline of actions
        int                                       m_UndoIndex = 0;  // Current position in global history
    };
}
#endif