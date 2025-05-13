#ifndef XUNDO_EXAMPLE_HISTORY_H
#define XUNDO_EXAMPLE_HISTORY_H
#pragma once

#include "xundo_history.h"
#include "xundo_example_system.h"

//-----------------------------------------------------------------------------------------------------------
// Stress Test Example: Exercises history with multiple systems and multi-system commands
//-----------------------------------------------------------------------------------------------------------
namespace xundo::history_example
{
    using namespace xundo::system_example;

    // Stress Test: Single-system focus to test history system
    int StressTestHistoryMultiSystem()
    {
        // Setup: Three systems—Graphics, Physics, Audio
        history Hist;
        fake_dbase GraphicsData, PhysicsData, AudioData;
        system GraphicsSys, PhysicsSys, AudioSys;
        MovePoint GraphicsCmd(GraphicsSys, &GraphicsData);
        MovePoint PhysicsCmd(PhysicsSys, &PhysicsData);
        MovePoint AudioCmd(AudioSys, &AudioData);

        if (auto err = GraphicsSys.Init({}, false); err.empty() == false)
        {
            std::cerr << err << "\n";
            return 1;
        }

        if (auto err = PhysicsSys.Init({}, false); err.empty() == false)
        {
            std::cerr << err << "\n";
            return 1;
        }

        if (auto err = AudioSys.Init({}, false); err.empty() == false)
        {
            std::cerr << err << "\n";
            return 1;
        }

        // Register systems with GUIDs
        constexpr uint64_t GUIDGraphics = 1;
        constexpr uint64_t GUIDPhysics = 2;
        constexpr uint64_t GUIDAudio = 3;
        Hist.AddSystem("Graphics", GUIDGraphics, GraphicsSys);
        Hist.AddSystem("Physics", GUIDPhysics, PhysicsSys);
        Hist.AddSystem("Audio", GUIDAudio, AudioSys);

        // User 1: Build a scene—single-system moves
        for (int i = 0; i < 3; ++i)
        {
            // Graphics moves
            if (auto Err = Hist.Execute(GUIDGraphics, MovePoint::Move(i * 10, i * 10), 1); !Err.empty())
            {
                std::cout << Err << "\n";
                assert(false);
            }

            // Physics follows
            if (auto Err = Hist.Execute(GUIDPhysics, MovePoint::Move(i * 5, i * 5), 1); !Err.empty())
            {
                std::cout << Err << "\n";
                assert(false);
            }
        }
        std::cout << "After User 1 builds scene:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 20 && GraphicsData.m_Y == 20 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        // User 2: Single-system move—no multi-system sync
        if (auto Err = Hist.Execute(GUIDGraphics, MovePoint::Move(0, 0), 2); !Err.empty())
        {
            std::cout << Err << "\n";
            assert(false);
        }
        std::cout << "\nAfter User 2 moves Graphics:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0); // Physics, Audio unchanged

        // User 1: Add more single-system moves
        if (auto Err = Hist.Execute(GUIDAudio, MovePoint::Move(30, 30), 1); !Err.empty())
        {
            std::cout << Err << "\n";
            assert(false);
        }
        if (auto Err = Hist.Execute(GUIDGraphics, MovePoint::Move(10, 10), 1); !Err.empty())
        {
            std::cout << Err << "\n";
            assert(false);
        }
        std::cout << "\nAfter User 1 adds moves:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 10 && GraphicsData.m_Y == 10 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 30 && AudioData.m_Y == 30);

        // Stress: Undo/Redo mix
        Hist.UndoSystem(GUIDGraphics); // Undo Graphics single—ok
        std::cout << "\nAfter UndoSystem Graphics:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 30 && AudioData.m_Y == 30);

        Hist.UndoSystem(GUIDAudio); // Undo Audio single—ok
        std::cout << "\nAfter UndoSystem Audio:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        Hist.Undo(); // Undo Graphics single
        std::cout << "\nAfter global Undo (Graphics):\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 20 && GraphicsData.m_Y == 20 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        Hist.RedoSystem(GUIDGraphics); // Redo Graphics single
        std::cout << "\nAfter RedoSystem Graphics:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        // User 2: Group command—single-system focus
        std::vector<std::string> GroupCmds = {
            MovePoint::Move(40, 40),
            MovePoint::Move(50, 50)
        };
        if (auto Err = Hist.Execute(GUIDPhysics, "Group MoveSync", GroupCmds, 2); !Err.empty())
        {
            std::cout << Err << "\n";
            assert(false);
        }
        std::cout << "\nAfter User 2 group sync:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 50 && PhysicsData.m_Y == 50 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        // Undo/Redo stress
        Hist.Undo(); // Undo group
        std::cout << "\nAfter global Undo (Group):\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        Hist.Redo(); // Redo group
        std::cout << "\nAfter global Redo (Group):\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 50 && PhysicsData.m_Y == 50 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        // Final chaos: UndoSystem, RedoSystem mix
        Hist.UndoSystem(GUIDPhysics); // Undo Physics group—ok
        std::cout << "\nAfter UndoSystem Physics:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 10 && PhysicsData.m_Y == 10 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        Hist.RedoSystem(GUIDPhysics); // Redo Physics group
        std::cout << "\nAfter RedoSystem Physics:\n";
        Hist.DisplayHistory();
        assert(GraphicsData.m_X == 0 && GraphicsData.m_Y == 0 &&
            PhysicsData.m_X == 50 && PhysicsData.m_Y == 50 &&
            AudioData.m_X == 0 && AudioData.m_Y == 0);

        std::cout << "Stress test completed successfully!\n";
        return 0;
    }
}

#endif