#include "skiplist.hpp"

#include <string>
#include <fstream>
#include <iostream>

template<typename K, typename V>
class KVPersister {
private:
    std::string _filepath;
    std::string _delimiter;

public:
    KVPersister(const std::string& filepath, const std::string& delimiter = ":") 
        : _filepath(filepath), _delimiter(delimiter) {}

    void dump(const SkipList<K, V>& skiplist) {
        std::ofstream file_writer(_filepath);
        if (file_writer.is_open() == false) {
            std::cerr << "Error: Unable to open file for writing." << std::endl;
            return;
        }

        skiplist.traverse([this](const K& key, const V& value) {
            this->file_writer << key << this->_delimiter << value << "\n";
        });
        
        file_writer.flush();
        file_writer.close();
    }

    void load(SkipList<K, V>& skiplist) {
        std::ifstream file_reader(_filepath);
        if (file_reader.is_open() == false) {
            std::cerr << "Error: Unable to open file for reading." << std::endl;
            return;
        }

        std::string line;
        while (std::getline(file_reader, line)) {
            if (line.empty() )
                continue;

            size_t pos = line.find(_delimiter);
            if (pos != std::string::npos) {
                std::string key_str = line.substr(0, pos);
                std::string value_str = line.substr(pos + _delimiter.length() );

                if (key_str.empty() || value_str.empty() ) 
                    continue;

                try {
                    int key = std::stoi(key_str);
                    skiplist.insert_element(key, value_str); 
                } catch (const std::exception& e) {
                    continue; 
                }
            }
        }
        file_reader.close();
    }
};