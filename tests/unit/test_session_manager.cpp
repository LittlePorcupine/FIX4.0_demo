#include "../catch2/catch.hpp"
#include "fix/session_manager.hpp"
#include "fix/session.hpp"

using namespace fix40;

TEST_CASE("SessionManager - Basic operations", "[session_manager]") {
    SessionManager manager;
    
    SECTION("Initially empty") {
        REQUIRE(manager.getSessionCount() == 0);
        REQUIRE_FALSE(manager.hasSession(SessionID("A", "B")));
    }
    
    SECTION("Register and find session") {
        auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
        manager.registerSession(session);
        
        REQUIRE(manager.getSessionCount() == 1);
        REQUIRE(manager.hasSession(SessionID("CLIENT", "SERVER")));
        
        auto found = manager.findSession(SessionID("CLIENT", "SERVER"));
        REQUIRE(found != nullptr);
        REQUIRE(found.get() == session.get());
    }
    
    SECTION("Unregister session") {
        auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
        manager.registerSession(session);
        
        REQUIRE(manager.unregisterSession(SessionID("CLIENT", "SERVER")));
        REQUIRE(manager.getSessionCount() == 0);
        REQUIRE_FALSE(manager.hasSession(SessionID("CLIENT", "SERVER")));
    }
    
    SECTION("Unregister non-existent session returns false") {
        REQUIRE_FALSE(manager.unregisterSession(SessionID("A", "B")));
    }
    
    SECTION("Find non-existent session returns nullptr") {
        auto found = manager.findSession(SessionID("A", "B"));
        REQUIRE(found == nullptr);
    }
    
    SECTION("Replace existing session") {
        auto session1 = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
        auto session2 = std::make_shared<Session>("CLIENT", "SERVER", 60, [](){});
        
        manager.registerSession(session1);
        manager.registerSession(session2);
        
        REQUIRE(manager.getSessionCount() == 1);
        
        auto found = manager.findSession(SessionID("CLIENT", "SERVER"));
        REQUIRE(found.get() == session2.get());
    }
    
    SECTION("Multiple sessions") {
        auto session1 = std::make_shared<Session>("CLIENT1", "SERVER", 30, [](){});
        auto session2 = std::make_shared<Session>("CLIENT2", "SERVER", 30, [](){});
        auto session3 = std::make_shared<Session>("CLIENT3", "SERVER", 30, [](){});
        
        manager.registerSession(session1);
        manager.registerSession(session2);
        manager.registerSession(session3);
        
        REQUIRE(manager.getSessionCount() == 3);
        REQUIRE(manager.hasSession(SessionID("CLIENT1", "SERVER")));
        REQUIRE(manager.hasSession(SessionID("CLIENT2", "SERVER")));
        REQUIRE(manager.hasSession(SessionID("CLIENT3", "SERVER")));
    }
    
    SECTION("forEachSession iterates all sessions") {
        auto session1 = std::make_shared<Session>("CLIENT1", "SERVER", 30, [](){});
        auto session2 = std::make_shared<Session>("CLIENT2", "SERVER", 30, [](){});
        
        manager.registerSession(session1);
        manager.registerSession(session2);
        
        int count = 0;
        manager.forEachSession([&](const SessionID&, std::shared_ptr<Session>) {
            count++;
        });
        
        REQUIRE(count == 2);
    }

    SECTION("forEachSession callback can re-enter SessionManager without deadlock") {
        auto session1 = std::make_shared<Session>("CLIENT1", "SERVER", 30, [](){});
        auto session2 = std::make_shared<Session>("CLIENT2", "SERVER", 30, [](){});

        manager.registerSession(session1);
        manager.registerSession(session2);

        // 之前实现会持锁调用 callback，这里在 callback 内调用 unregister 会发生死锁。
        manager.forEachSession([&](const SessionID& id, std::shared_ptr<Session>) {
            manager.unregisterSession(id);
        });

        REQUIRE(manager.getSessionCount() == 0);
    }
}

TEST_CASE("SessionManager - sendMessage", "[session_manager]") {
    SessionManager manager;
    
    SECTION("sendMessage to non-existent session returns false") {
        FixMessage msg;
        msg.set(35, "8");
        
        REQUIRE_FALSE(manager.sendMessage(SessionID("A", "B"), msg));
    }
    
    SECTION("sendMessage to non-running session returns false") {
        auto session = std::make_shared<Session>("CLIENT", "SERVER", 30, [](){});
        manager.registerSession(session);
        // Session not started, so is_running() returns false
        
        FixMessage msg;
        msg.set(35, "8");
        
        REQUIRE_FALSE(manager.sendMessage(SessionID("CLIENT", "SERVER"), msg));
    }
}

TEST_CASE("SessionIDHash", "[session_manager]") {
    SessionIDHash hasher;
    
    SessionID id1("A", "B");
    SessionID id2("A", "B");
    SessionID id3("B", "A");
    
    // 相同的 SessionID 应该产生相同的哈希值
    REQUIRE(hasher(id1) == hasher(id2));
    
    // 不同的 SessionID 应该产生不同的哈希值（虽然理论上可能碰撞，但这个简单例子不会）
    REQUIRE(hasher(id1) != hasher(id3));
}
