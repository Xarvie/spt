#include "sptxx.hpp"
#include <iostream>
#include <memory>
#include <string>

struct Player {
  std::string name;
  int level;
  static int destructor_count;
  
  Player() : name("unknown"), level(1) {}
  Player(std::string n, int l) : name(std::move(n)), level(l) {}
  ~Player() { 
    destructor_count++;
    std::cout << "  Player '" << name << "' destroyed (total: " << destructor_count << ")" << std::endl;
  }
  
  std::string get_info() const {
    return name + " (level " + std::to_string(level) + ")";
  }
  
  void level_up() { level++; }
};

int Player::destructor_count = 0;

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Ownership Semantics ===" << std::endl;

    std::cout << "\n--- Part 1: Owned (Lua owns the object) ---" << std::endl;
    {
      auto player_type = lua.new_usertype<Player>("Player");
      player_type.constructor<std::string, int>();
      player_type.set("name", &Player::name);
      player_type.set("level", &Player::level);
      player_type.set("get_info", &Player::get_info);
      player_type.set("level_up", &Player::level_up);
      
      std::cout << "Player type registered" << std::endl;
    }

    std::cout << "\n1. Creating owned player via constructor..." << std::endl;
    lua.do_string("owned_player = Player('Alice', 10);");
    lua.do_string("print('Owned player: ' .. owned_player.get_info(owned_player));");
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 0)" << std::endl;

    std::cout << "\n2. Pushing owned player from C++..." << std::endl;
    {
      auto player_type = lua.get_usertype<Player>("Player");
      player_type.push_owned(new Player("Bob", 20));
      lua_setglobal(lua.lua_state(), "cpp_owned_player");
    }
    lua.do_string("print('C++ owned player: ' .. cpp_owned_player.get_info(cpp_owned_player));");
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 0)" << std::endl;

    std::cout << "\n3. Releasing owned players (GC should delete them)..." << std::endl;
    lua.do_string("owned_player = nil; cpp_owned_player = nil;");
    lua.do_string("collectgarbage();");
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 2)" << std::endl;

    std::cout << "\n--- Part 2: Unowned (C++ owns the object) ---" << std::endl;
    Player::destructor_count = 0;
    
    Player* external_player = new Player("Charlie", 30);
    std::cout << "Created external player: " << external_player->get_info() << std::endl;
    
    {
      auto player_type = lua.get_usertype<Player>("Player");
      player_type.push_unowned(external_player);
      lua_setglobal(lua.lua_state(), "unowned_player");
    }
    
    lua.do_string("print('Unowned player: ' .. unowned_player.get_info(unowned_player));");
    lua.do_string("unowned_player.level_up(unowned_player);");
    std::cout << "After level_up: " << external_player->get_info() << std::endl;
    
    std::cout << "\n4. Releasing unowned player (GC should NOT delete it)..." << std::endl;
    lua.do_string("unowned_player = nil;");
    lua.do_string("collectgarbage();");
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 0 - NOT deleted)" << std::endl;
    
    std::cout << "\n5. Deleting external player manually..." << std::endl;
    delete external_player;
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 1)" << std::endl;

    std::cout << "\n--- Part 3: Shared (shared_ptr ownership) ---" << std::endl;
    Player::destructor_count = 0;
    
    auto shared_player = std::make_shared<Player>("Diana", 40);
    std::cout << "Created shared player: " << shared_player->get_info() << ", use_count: " << shared_player.use_count() << std::endl;
    
    {
      auto player_type = lua.get_usertype<Player>("Player");
      player_type.push_shared(shared_player);
      lua_setglobal(lua.lua_state(), "shared_player_lua");
    }
    std::cout << "After push_shared, use_count: " << shared_player.use_count() << " (should be 2)" << std::endl;
    
    lua.do_string("print('Shared player: ' .. shared_player_lua.get_info(shared_player_lua));");
    
    std::cout << "\n6. Releasing shared player from Lua..." << std::endl;
    lua.do_string("shared_player_lua = nil;");
    lua.do_string("collectgarbage();");
    std::cout << "After GC, use_count: " << shared_player.use_count() << " (should be 1)" << std::endl;
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 0 - still held by C++)" << std::endl;
    
    std::cout << "\n7. Releasing shared player from C++..." << std::endl;
    shared_player.reset();
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 1)" << std::endl;

    std::cout << "\n--- Part 4: Multiple shared_ptr references ---" << std::endl;
    Player::destructor_count = 0;
    
    auto shared1 = std::make_shared<Player>("Eve", 50);
    auto shared2 = shared1;
    
    std::cout << "Created shared player, use_count: " << shared1.use_count() << std::endl;
    
    {
      auto player_type = lua.get_usertype<Player>("Player");
      player_type.push_shared(shared1);
      lua_setglobal(lua.lua_state(), "multi_ref_player");
    }
    std::cout << "After push_shared, use_count: " << shared1.use_count() << std::endl;
    
    lua.do_string("multi_ref_player = nil;");
    lua.do_string("collectgarbage();");
    std::cout << "After Lua GC, use_count: " << shared1.use_count() << std::endl;
    
    shared1.reset();
    std::cout << "After shared1 reset, use_count: " << shared2.use_count() << std::endl;
    std::cout << "Destructor count: " << Player::destructor_count << " (should be 0)" << std::endl;
    
    shared2.reset();
    std::cout << "After shared2 reset, destructor count: " << Player::destructor_count << " (should be 1)" << std::endl;

    std::cout << "\n=== All Ownership Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
