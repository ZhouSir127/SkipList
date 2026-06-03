/* ************************************************************************
 > File Name:     skiplist.hpp
 > Description:   High-Performance SkipList (KV Storage Engine)
 ************************************************************************/

#ifndef SKIPLIST_HPP
#define SKIPLIST_HPP

#include <iostream> 
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <string>
#include <random>
#include <vector>

#include <new>          // 必须引入，用于 Placement New 连续内存分配
#include <mutex>        // 用于 std::unique_lock
#include <shared_mutex> // 用于读写锁 std::shared_mutex 和 std::shared_lock (需要 C++17)

// 节点类模板
template<typename K, typename V> 
class Node {
public:
    K key;
    V value;
    int node_level;
     
    Node<K, V>** forward; 

    Node(int level,const K& k = K(), const V& v = V() ) 
        : key(k), value(v), node_level(level) {
    
        forward = reinterpret_cast<Node**>(this + 1);
        memset(forward, 0, sizeof(Node*) * (level + 1));
    }

    void* operator new(size_t size ,int level) {
        return ::operator new(size + sizeof(Node<K, V>*) * (level + 1) );
    }
    
    void operator delete(void* ptr) {
        ::operator delete(ptr);
    }

    K get_key() const { return key; }
    V get_value() const { return value; }
    void set_value(const V& val) { value = val; }
};

// 跳表类模板
template <typename K, typename V> 
class SkipList {
public: 
    explicit SkipList(int max_level);
    ~SkipList();
    
    int insert_element(const K& key, const V& value);
    bool search_element(const K& key);
    void delete_element(const K& key);
    
    void display_list();
    void dump_file(const std::string& filepath = "store/dumpFile");
    void load_file(const std::string& filepath = "store/dumpFile");
    
    void clear();
    int size() const;

private:
    int get_random_level();
    Node<K, V>* create_node(int level, const K& k, const V& v);
    void get_key_value_from_string(const std::string& str, std::string* key, std::string* value);
    bool is_valid_string(const std::string& str);
    
    int _max_level;
    int _skip_list_level;
    Node<K, V>* _header;
    int _element_count;

    mutable std::shared_timed_mutex _rw_mutex;
    std::string _delimiter = ":";
};


template<typename K, typename V> 
SkipList<K, V>::SkipList(int max_level)
: _max_level(max_level), _skip_list_level(0), _element_count(0),_header( new(sizeof(Node<K,V>),_max_level) Node<K, V>(_max_level) ) 
{}

template<typename K, typename V> 
SkipList<K, V>::~SkipList() {
    clear();
    delete _header;
}

template <typename K, typename V>
void SkipList<K, V>::clear() {
    std::unique_lock<std::shared_timed_mutex> lock(_rw_mutex);
    Node<K, V>* current = _header->forward[0];
    while (current) {
        Node<K, V>* death = current;
        current = current->forward[0];
        delete death; 
    }
    memset(_header->forward, 0, sizeof(Node<K, V>*) * (_max_level + 1) );
    _skip_list_level = 0;
    _element_count = 0;
}

template<typename K, typename V>
Node<K, V>* SkipList<K, V>::create_node(int level, const K& k, const V& v) {
    return new(level) Node<K, V>(level, k, v);
}

//[1, _max_level]
template<typename K, typename V>
int SkipList<K, V>::get_random_level() {
    static thread_local std::mt19937 generator(std::random_device{}());
    static thread_local std::uniform_int_distribution<int> distribution(0, 1);
    
    int k = 1;
    while (distribution(generator) && k < _max_level)
        ++k;
    
    return k;
}

template<typename K, typename V>
int SkipList<K, V>::insert_element(const K& key, const V& value) {
    std::unique_lock<std::shared_timed_mutex> lock(_rw_mutex); 
    
    Node<K, V>* current = _header;
    std::vector<Node<K, V>*> update(_max_level + 1, nullptr);

    for(int i = _skip_list_level; i >= 0; --i){
        while(current->forward[i])
            if( current->forward[i]->get_key() == key ){
                current->forward[i]->set_value(value);
                return 1; // Key 已存在，直接返回 
            }else if( current->forward[i]->get_key() < key )
                current = current->forward[i]; 
            else      
                break;

        update[i] = current;//记录每层的可能待插入节点的前驱节点，后续插入新节点时需要更新这些前驱节点的 forward 指针  
    }
        
    int random_level = get_random_level();
    if (random_level > _skip_list_level) {
        for (int i = _skip_list_level + 1; i <= random_level; ++i)
            update[i] = _header;
        
        _skip_list_level = random_level;
    }

    Node<K, V>* inserted_node = create_node(random_level, key, value);
    for (int i = 0; i <= random_level; i++) {
        inserted_node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = inserted_node;
    }
    
    ++_element_count;
    return 0;
}

template<typename K, typename V> 
bool SkipList<K, V>::search_element(const K& key) {//curr指向目标的局部(层)最大前驱节点
    std::shared_lock<std::shared_timed_mutex> lock(_rw_mutex);
    
    Node<K, V>* current = _header;
    for (int i = _skip_list_level; i >= 0; --i)//固定层数，从最高层开始向下查找，如果下一层curr的后面有比当前curr的k更加逼近目标k的节点则更新curr
        while (current->forward[i] ){
            if(current->forward[i]->get_key() == key) 
                return true; //当前层的下一个节点的k=目标k了(找到了) 
            else if(current->forward[i]->get_key() < key)
                current = current->forward[i];
            else
                break; //当前层没有目标k，向下一层继续找
        }
    return false;
}

template<typename K, typename V> 
void SkipList<K, V>::delete_element(const K& key) {
    std::unique_lock<std::shared_timed_mutex> lock(_rw_mutex);
    
    Node<K, V>* current = _header; 
    std::vector<Node<K, V>*> update(_max_level + 1, nullptr);

    for (int i = _skip_list_level; i >= 0; --i) {
        while (current->forward[i] ){
            if(current->forward[i]->get_key() < key ) 
                current = current->forward[i];
            else if(current->forward[i]->get_key() == key) 
                update[i] = current; //当前层的下一个节点的k=目标k了(找到了)，记录当前层的前驱节点，后续删除目标节点时需要更新这些前驱节点的 forward 指针 
            else
                break; //当前层没有目标k，向下一层继续
        }
    }//update记录每一层目标节点的可能前驱

    current = current->forward[0];
    if (current && current->get_key() == key) {
        for (int i = 0; i <= _skip_list_level; i++) {
            if (update[i]->forward[i] != current) break;
            update[i]->forward[i] = current->forward[i];
        }

        while (_skip_list_level > 0 && _header->forward[_skip_list_level] == nullptr) {
            _skip_list_level--; 
        }

        delete current;
        _element_count--;
    }
}

template<typename K, typename V> 
int SkipList<K, V>::size() const { 
    std::shared_lock<std::shared_timed_mutex> lock(_rw_mutex);
    return _element_count;
}

template<typename K, typename V> 
void SkipList<K, V>::display_list() {
    std::shared_lock<std::shared_timed_mutex> lock(_rw_mutex);
    for (int i = _skip_list_level; i >= 0; --i) {
        std::cout << "Level " << i << ": ";
        Node<K, V>* node = _header->forward[i]; 
        while (node) {
            std::cout << node->get_key() << ":" << node->get_value() << "; ";
            node = node->forward[i];
        }
        std::cout << std::endl;
    }
}

// 【核心优化5】：将文件流对象设计为局部变量，随用随关，避免占用系统 fd 资源
template<typename K, typename V> 
void SkipList<K, V>::dump_file(const std::string& filepath) {
    std::shared_lock<std::shared_timed_mutex> lock(_rw_mutex);
    std::ofstream file_writer(filepath);
    if (!file_writer.is_open()) return;

    Node<K, V>* node = _header->forward[0]; 
    while (node != nullptr) {
        file_writer << node->get_key() << _delimiter << node->get_value() << "\n";
        node = node->forward[0];
    }
    file_writer.flush();
    file_writer.close();
}

template<typename K, typename V> 
void SkipList<K, V>::load_file(const std::string& filepath) {
    std::shared_lock<std::shared_timed_mutex> lock(_rw_mutex);
    std::ifstream file_reader(filepath);
    if (!file_reader.is_open()) return;

    std::string line;
    std::string key_str, value_str;
    while (getline(file_reader, line)) {
        get_key_value_from_string(line, &key_str, &value_str);
        if (key_str.empty() || value_str.empty()) continue;
        
        // 假定 K 为 int, V 为 std::string，工业级项目中此处应引入泛型反序列化组件
        insert_element(std::stoi(key_str), value_str); 
    }
    file_reader.close();
}

template<typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string& str, std::string* key, std::string* value) {
    if(!is_valid_string(str)) return;
    *key = str.substr(0, str.find(_delimiter));
    *value = str.substr(str.find(_delimiter) + 1, str.length());
}

template<typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string& str) {
    return !str.empty() && str.find(_delimiter) != std::string::npos;
}

#endif // SKIPLIST_HPP