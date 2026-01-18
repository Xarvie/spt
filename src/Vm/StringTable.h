#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace spt {

// ---------
// - 查找：O(1) 平均，主位置命中率极高
// - 插入：O(1) 平均
// - 删除：O(1) 平均
// - 内存：每个槽位 sizeof(Node) = 16-24 字节
// ============================================================================

struct StringObject;

// ============================================================================
struct IdentityStringHash {
  static uint32_t hash(const StringObject *s) noexcept;
};

// ============================================================================
// SptHashTable - 核心哈希表实现
// ============================================================================
template <typename V> class SptHashTable {
public:
  // 节点结构 - 内联存储键值对 + 碰撞链指针
  struct Node {
    StringObject *key; // 键（nil 表示空槽）
    V value;           // 值
    int next;          // 碰撞链下一个节点索引（-1 表示链尾）

    Node() : key(nullptr), value{}, next(-1) {}
  };

  SptHashTable() : nodes_(nullptr), size_(0), capacity_(0), lastFree_(0) {}

  ~SptHashTable() {
    if (nodes_) {
      std::free(nodes_);
    }
  }

  // 禁止拷贝
  SptHashTable(const SptHashTable &) = delete;
  SptHashTable &operator=(const SptHashTable &) = delete;

  // 移动语义
  SptHashTable(SptHashTable &&other) noexcept
      : nodes_(other.nodes_), size_(other.size_), capacity_(other.capacity_),
        lastFree_(other.lastFree_) {
    other.nodes_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    other.lastFree_ = 0;
  }

  SptHashTable &operator=(SptHashTable &&other) noexcept {
    if (this != &other) {
      if (nodes_)
        std::free(nodes_);
      nodes_ = other.nodes_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      lastFree_ = other.lastFree_;
      other.nodes_ = nullptr;
      other.size_ = 0;
      other.capacity_ = 0;
      other.lastFree_ = 0;
    }
    return *this;
  }

  // === 核心操作 ===

  // 查找 - O(1) 平均，最坏 O(n)
  V *get(StringObject *key) noexcept {
    if (!nodes_ || !key)
      return nullptr;

    uint32_t h = IdentityStringHash::hash(key);
    uint32_t idx = h & (capacity_ - 1); // 直接 hash & mask

    Node *n = &nodes_[idx];

    // 主位置检查
    if (n->key == key)
      return &n->value;
    if (!n->key)
      return nullptr; // 空槽

    // 遍历碰撞链
    int nextIdx = n->next;
    while (nextIdx >= 0) {
      n = &nodes_[nextIdx];
      if (n->key == key)
        return &n->value;
      nextIdx = n->next;
    }

    return nullptr;
  }

  // const 版本
  const V *get(StringObject *key) const noexcept {
    return const_cast<SptHashTable *>(this)->get(key);
  }

  // 设置 - 如果键存在则更新，否则插入
  void set(StringObject *key, const V &value) {
    if (!key)
      return;

    // 确保有足够容量
    if (!nodes_ || size_ >= capacity_ * 3 / 4) {
      rehash(capacity_ == 0 ? 8 : capacity_ * 2);
    }

    uint32_t h = IdentityStringHash::hash(key);
    uint32_t mainIdx = h & (capacity_ - 1);

    Node *mainNode = &nodes_[mainIdx];

    // Case 1: 主位置为空
    if (!mainNode->key) {
      mainNode->key = key;
      mainNode->value = value;
      mainNode->next = -1;
      ++size_;
      return;
    }

    // Case 2: 主位置已被同键占用
    if (mainNode->key == key) {
      mainNode->value = value;
      return;
    }

    // Case 3: 检查碰撞链中是否已存在
    int prevIdx = mainIdx;
    int currIdx = mainNode->next;
    while (currIdx >= 0) {
      Node *curr = &nodes_[currIdx];
      if (curr->key == key) {
        curr->value = value;
        return;
      }
      prevIdx = currIdx;
      currIdx = curr->next;
    }

    // Case 4: 需要插入新节点
    // 检查主位置的占用者是否属于该位置
    uint32_t occupierHash = IdentityStringHash::hash(mainNode->key);
    uint32_t occupierMain = occupierHash & (capacity_ - 1);

    if (occupierMain == mainIdx) {
      // 占用者在正确位置，新键加入碰撞链
      int freeIdx = findFreeSlot();
      if (freeIdx < 0) {
        rehash(capacity_ * 2);
        set(key, value); // 重新尝试
        return;
      }

      Node *freeNode = &nodes_[freeIdx];
      freeNode->key = key;
      freeNode->value = value;
      freeNode->next = mainNode->next;
      mainNode->next = freeIdx;
      ++size_;
    } else {
      // 占用者不属于此位置（碰撞溢出），需要移走它
      // 找到指向 occupier 的前驱节点
      int predIdx = findPredecessor(mainIdx);

      // 找到空槽移动 occupier
      int freeIdx = findFreeSlot();
      if (freeIdx < 0) {
        rehash(capacity_ * 2);
        set(key, value);
        return;
      }

      // 移动 occupier 到空槽
      Node *freeNode = &nodes_[freeIdx];
      *freeNode = *mainNode;

      // 更新前驱的 next 指针
      if (predIdx >= 0) {
        nodes_[predIdx].next = freeIdx;
      }

      // 主位置现在归新键所有
      mainNode->key = key;
      mainNode->value = value;
      mainNode->next = -1;
      ++size_;
    }
  }

  // 删除
  bool remove(StringObject *key) noexcept {
    if (!nodes_ || !key)
      return false;

    uint32_t h = IdentityStringHash::hash(key);
    uint32_t mainIdx = h & (capacity_ - 1);

    Node *mainNode = &nodes_[mainIdx];

    // 主位置匹配
    if (mainNode->key == key) {
      if (mainNode->next >= 0) {
        // 用链中下一个节点替换
        Node *nextNode = &nodes_[mainNode->next];
        int savedNext = nextNode->next;
        mainNode->key = nextNode->key;
        mainNode->value = nextNode->value;
        mainNode->next = savedNext;
        // 清空被移动的节点
        nextNode->key = nullptr;
        nextNode->value = V{};
        nextNode->next = -1;
      } else {
        mainNode->key = nullptr;
        mainNode->value = V{};
        mainNode->next = -1;
      }
      --size_;
      return true;
    }

    // 搜索碰撞链
    int prevIdx = mainIdx;
    int currIdx = mainNode->next;
    while (currIdx >= 0) {
      Node *curr = &nodes_[currIdx];
      if (curr->key == key) {
        // 从链中移除
        nodes_[prevIdx].next = curr->next;
        curr->key = nullptr;
        curr->value = V{};
        curr->next = -1;
        --size_;
        return true;
      }
      prevIdx = currIdx;
      currIdx = curr->next;
    }

    return false;
  }

  // 检查是否存在
  bool contains(StringObject *key) const noexcept { return get(key) != nullptr; }

  // 操作符重载
  V &operator[](StringObject *key) {
    V *existing = get(key);
    if (existing)
      return *existing;

    set(key, V{});
    return *get(key);
  }

  // === 状态查询 ===
  size_t size() const noexcept { return size_; }

  bool empty() const noexcept { return size_ == 0; }

  size_t capacity() const noexcept { return capacity_; }

  // === 迭代支持 ===

  // 前向声明
  class const_iterator;

  // 迭代器代理 - 支持 it->first/it->second 访问模式
  struct IteratorProxy {
    StringObject *first;
    V &second;

    IteratorProxy(StringObject *k, V &v) : first(k), second(v) {}

    IteratorProxy *operator->() { return this; }
  };

  struct ConstIteratorProxy {
    StringObject *first;
    const V &second;

    ConstIteratorProxy(StringObject *k, const V &v) : first(k), second(v) {}

    const ConstIteratorProxy *operator->() const { return this; }
  };

  class iterator {
  public:
    using value_type = std::pair<StringObject *, V &>;

    iterator(Node *nodes, size_t capacity, size_t idx)
        : nodes_(nodes), capacity_(capacity), idx_(idx) {
      skipEmpty();
    }

    bool operator==(const iterator &other) const { return idx_ == other.idx_; }

    bool operator!=(const iterator &other) const { return idx_ != other.idx_; }

    iterator &operator++() {
      ++idx_;
      skipEmpty();
      return *this;
    }

    std::pair<StringObject *, V &> operator*() { return {nodes_[idx_].key, nodes_[idx_].value}; }

    IteratorProxy operator->() { return IteratorProxy(nodes_[idx_].key, nodes_[idx_].value); }

    StringObject *key() const { return nodes_[idx_].key; }

    V &value() { return nodes_[idx_].value; }

  private:
    void skipEmpty() {
      while (idx_ < capacity_ && !nodes_[idx_].key) {
        ++idx_;
      }
    }

    Node *nodes_;
    size_t capacity_;
    size_t idx_;

    // 允许 const_iterator 访问
    friend class const_iterator;
  };

  iterator begin() { return iterator(nodes_, capacity_, 0); }

  iterator end() { return iterator(nodes_, capacity_, capacity_); }

  // const 迭代器
  class const_iterator {
  public:
    const_iterator(const Node *nodes, size_t capacity, size_t idx)
        : nodes_(nodes), capacity_(capacity), idx_(idx) {
      skipEmpty();
    }

    const_iterator(const iterator &it)
        : nodes_(it.nodes_), capacity_(it.capacity_), idx_(it.idx_) {}

    bool operator==(const const_iterator &other) const { return idx_ == other.idx_; }

    bool operator!=(const const_iterator &other) const { return idx_ != other.idx_; }

    const_iterator &operator++() {
      ++idx_;
      skipEmpty();
      return *this;
    }

    std::pair<StringObject *, const V &> operator*() const {
      return {nodes_[idx_].key, nodes_[idx_].value};
    }

    // STL 兼容：支持 it->first 和 it->second
    ConstIteratorProxy operator->() const {
      return ConstIteratorProxy(nodes_[idx_].key, nodes_[idx_].value);
    }

    StringObject *key() const { return nodes_[idx_].key; }

    const V &value() const { return nodes_[idx_].value; }

  private:
    void skipEmpty() {
      while (idx_ < capacity_ && !nodes_[idx_].key) {
        ++idx_;
      }
    }

    const Node *nodes_;
    size_t capacity_;
    size_t idx_;
  };

  const_iterator begin() const { return const_iterator(nodes_, capacity_, 0); }

  const_iterator end() const { return const_iterator(nodes_, capacity_, capacity_); }

  // STL 兼容的 find
  iterator find(StringObject *key) {
    if (!nodes_ || !key)
      return end();

    uint32_t h = IdentityStringHash::hash(key);
    uint32_t idx = h & (capacity_ - 1);

    Node *n = &nodes_[idx];
    if (n->key == key)
      return iterator(nodes_, capacity_, idx);
    if (!n->key)
      return end();

    int nextIdx = n->next;
    while (nextIdx >= 0) {
      if (nodes_[nextIdx].key == key) {
        return iterator(nodes_, capacity_, nextIdx);
      }
      nextIdx = nodes_[nextIdx].next;
    }

    return end();
  }

  const_iterator find(StringObject *key) const {
    return const_cast<SptHashTable *>(this)->find(key);
  }

  // 清空
  void clear() noexcept {
    if (nodes_) {
      for (size_t i = 0; i < capacity_; ++i) {
        nodes_[i].key = nullptr;
        nodes_[i].value = V{};
        nodes_[i].next = -1;
      }
    }
    size_ = 0;
    lastFree_ = capacity_;
  }

  // 预分配
  void reserve(size_t n) {
    if (n > capacity_) {
      size_t newCap = 8;
      while (newCap < n)
        newCap *= 2;
      rehash(newCap);
    }
  }

  // === GC 集成 ===

  // 遍历所有键值对（用于 GC 标记阶段）
  template <typename Func> void forEach(Func &&fn) const {
    if (!nodes_)
      return;
    for (size_t i = 0; i < capacity_; ++i) {
      if (nodes_[i].key) {
        fn(nodes_[i].key, nodes_[i].value);
      }
    }
  }

  // 仅遍历所有键（用于 StringPool 清理等）
  template <typename Func> void forEachKey(Func &&fn) const {
    if (!nodes_)
      return;
    for (size_t i = 0; i < capacity_; ++i) {
      if (nodes_[i].key) {
        fn(nodes_[i].key);
      }
    }
  }

  // 仅遍历所有值
  template <typename Func> void forEachValue(Func &&fn) const {
    if (!nodes_)
      return;
    for (size_t i = 0; i < capacity_; ++i) {
      if (nodes_[i].key) {
        fn(nodes_[i].value);
      }
    }
  }

  // 原地移除满足条件的元素（用于 GC 清除阶段）
  template <typename Pred> size_t removeIf(Pred &&pred) {
    if (!nodes_)
      return 0;
    size_t removed = 0;
    for (size_t i = 0; i < capacity_; ++i) {
      Node *node = &nodes_[i];
      if (!node->key)
        continue;
      // 检查主位置节点
      if (pred(node->key, node->value)) {
        // 需要删除主位置节点
        if (node->next >= 0) {
          // 用链中下一个节点替换
          Node *nextNode = &nodes_[node->next];
          int savedNext = nextNode->next;
          node->key = nextNode->key;
          node->value = nextNode->value;
          node->next = savedNext;
          // 清空被移动的节点
          nextNode->key = nullptr;
          nextNode->value = V{};
          nextNode->next = -1;
        } else {
          // 直接清空
          node->key = nullptr;
          node->value = V{};
          node->next = -1;
        }
        ++removed;
        --size_;
        // 重新检查当前位置（因为可能移入了新节点）
        --i;
        continue;
      }

      // 检查并清理链中的节点
      int prevIdx = static_cast<int>(i);
      int currIdx = node->next;
      while (currIdx >= 0) {
        Node *curr = &nodes_[currIdx];
        if (pred(curr->key, curr->value)) {
          // 从链中移除
          nodes_[prevIdx].next = curr->next;
          curr->key = nullptr;
          curr->value = V{};
          curr->next = -1;
          ++removed;
          --size_;
          currIdx = nodes_[prevIdx].next;
        } else {
          prevIdx = currIdx;
          currIdx = curr->next;
        }
      }
    }

    return removed;
  }

  // 获取内部节点数组（仅用于调试/高级操作）
  Node *nodes() noexcept { return nodes_; }

  const Node *nodes() const noexcept { return nodes_; }

private:
  // 重哈希
  void rehash(size_t newCapacity) {
    Node *oldNodes = nodes_;
    size_t oldCapacity = capacity_;

    // 分配新数组
    nodes_ = static_cast<Node *>(std::calloc(newCapacity, sizeof(Node)));
    if (!nodes_) {
      nodes_ = oldNodes;
      throw std::bad_alloc();
    }

    // 初始化新节点
    for (size_t i = 0; i < newCapacity; ++i) {
      nodes_[i].next = -1;
    }

    capacity_ = newCapacity;
    lastFree_ = newCapacity;
    size_ = 0;

    // 重新插入所有元素
    if (oldNodes) {
      for (size_t i = 0; i < oldCapacity; ++i) {
        if (oldNodes[i].key) {
          set(oldNodes[i].key, oldNodes[i].value);
        }
      }
      std::free(oldNodes);
    }
  }

  // 找到空槽（从后向前搜索）
  int findFreeSlot() noexcept {
    while (lastFree_ > 0) {
      --lastFree_;
      if (!nodes_[lastFree_].key) {
        return static_cast<int>(lastFree_);
      }
    }
    return -1;
  }

  // 找到指向目标节点的前驱
  int findPredecessor(uint32_t targetIdx) noexcept {
    // 计算目标节点当前占用者的主位置
    Node *target = &nodes_[targetIdx];
    uint32_t h = IdentityStringHash::hash(target->key);
    uint32_t mainIdx = h & (capacity_ - 1);

    if (mainIdx == targetIdx)
      return -1; // 没有前驱

    int idx = mainIdx;
    while (idx >= 0 && nodes_[idx].next != static_cast<int>(targetIdx)) {
      idx = nodes_[idx].next;
    }
    return idx;
  }

  Node *nodes_;
  size_t size_;
  size_t capacity_;
  size_t lastFree_; // 空槽搜索指针（从后向前）
};

} // namespace spt