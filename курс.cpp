#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>  // Додано для роботи з файлами
#include <vector>   // Додано для зручного збереження інформації

// Структура заголовка блоку пам'яті (метадані)
struct alloc_t {
    uint32_t signature;  // Магічне число для перевірки цілісності (0xDEADBEEF)
    size_t size;         // Корисний розмір цього блоку
    size_t prev_size;    // Розмір попереднього сусіднього блоку (для склеювання ліворуч)
    int status;          // 1 - зайнято, 0 - вільно
};

// Вузол AVL-дерева, яке сортує ВІЛЬНІ блоки за їхнім розміром
struct node_t {
    size_t key;          // Ключ - це розмір вільного блоку
    uintptr_t val;       // Значення - адреса початку корисних даних у цьому блоці
    int height;          // Висота вузла (потрібна для балансування дерева)
    node_t* left;
    node_t* right;
};

// Проста реалізація AVL-дерева пошуку для нашого менеджера
class AvlTree {
private:
    node_t* root = nullptr;

    int height(node_t* n) { return n ? n->height : 0; }
    int getBalance(node_t* n) { return n ? height(n->left) - height(n->right) : 0; }

    node_t* rightRotate(node_t* y) {
        node_t* x = y->left;
        node_t* T2 = x->right;
        x->right = y;
        y->left = T2;
        y->height = std::max(height(y->left), height(y->right)) + 1;
        x->height = std::max(height(x->left), height(x->right)) + 1;
        return x;
    }

    node_t* leftRotate(node_t* x) {
        node_t* y = x->right;
        node_t* T2 = y->left;
        y->left = x;
        x->right = T2;
        x->height = std::max(height(x->left), height(x->right)) + 1;
        y->height = std::max(height(y->left), height(y->right)) + 1;
        return y;
    }

    node_t* insert(node_t* node, size_t key, uintptr_t val) {
        if (!node) return new node_t{key, val, 1, nullptr, nullptr};
        if (key < node->key) node->left = insert(node->left, key, val);
        else node->right = insert(node->right, key, val);

        node->height = 1 + std::max(height(node->left), height(node->right));
        int balance = getBalance(node);

        if (balance > 1 && key < node->left->key) return rightRotate(node);
        if (balance < -1 && key > node->right->key) return leftRotate(node);
        if (balance > 1 && key > node->left->key) { node->left = leftRotate(node->left); return rightRotate(node); }
        if (balance < -1 && key < node->right->key) { node->right = rightRotate(node->right); return leftRotate(node); }
        return node;
    }

    node_t* minValueNode(node_t* node) {
        node_t* current = node;
        while (current->left != nullptr) current = current->left;
        return current;
    }

    node_t* remove(node_t* rootNode, size_t key, uintptr_t val) {
        if (!rootNode) return rootNode;
        if (key < rootNode->key) rootNode->left = remove(rootNode->left, key, val);
        else if (key > rootNode->key) rootNode->right = remove(rootNode->right, key, val);
        else {
            if (rootNode->val != val) {
                rootNode->right = remove(rootNode->right, key, val);
                return rootNode;
            }
            if (!rootNode->left || !rootNode->right) {
                node_t* temp = rootNode->left ? rootNode->left : rootNode->right;
                if (!temp) { temp = rootNode; rootNode = nullptr; }
                else *rootNode = *temp;
                delete temp;
            } else {
                node_t* temp = minValueNode(rootNode->right);
                rootNode->key = temp->key;
                rootNode->val = temp->val;
                rootNode->right = remove(rootNode->right, temp->key, temp->val);
            }
        }
        if (!rootNode) return rootNode;

        rootNode->height = 1 + std::max(height(rootNode->left), height(rootNode->right));
        int balance = getBalance(rootNode);

        if (balance > 1 && getBalance(rootNode->left) >= 0) return rightRotate(rootNode);
        if (balance > 1 && getBalance(rootNode->left) < 0) { rootNode->left = leftRotate(rootNode->left); return rightRotate(rootNode); }
        if (balance < -1 && getBalance(rootNode->right) <= 0) return leftRotate(rootNode);
        if (balance < -1 && getBalance(rootNode->right) > 0) { rootNode->right = rightRotate(rootNode->right); return leftRotate(rootNode); }
        return rootNode;
    }

    node_t* find_best_fit(node_t* node, size_t size) {
        if (!node) return nullptr;
        if (node->key >= size) {
            node_t* left_res = find_best_fit(node->left, size);
            return left_res ? left_res : node;
        }
        return find_best_fit(node->right, size);
    }

public:
    void insert_block(size_t key, uintptr_t val) { root = insert(root, key, val); }
    void remove_block(size_t key, uintptr_t val) { root = remove(root, key, val); }
    node_t* search_fit(size_t size) { return find_best_fit(root, size); }
};

// Головний C++ менеджер пам'яті
class AdvancedMemoryManager {
private:
    void* start_pool;
    size_t total_pool_sz;
    AvlTree freeTree; // Дерево вільних блоків

    size_t total_allocations = 0;
    size_t total_frees = 0;

    void PrintStatistics() {
        size_t used = 0;
        size_t free_mem = 0;
        size_t free_blocks = 0;
        size_t largest_free = 0;

        uintptr_t curr = reinterpret_cast<uintptr_t>(start_pool);
        while (curr < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
            alloc_t* block = reinterpret_cast<alloc_t*>(curr);
            if (block->status == 1) {
                used += block->size;
            } else {
                free_mem += block->size;
                free_blocks++;
                if (block->size > largest_free)
                    largest_free = block->size;
            }
            curr += sizeof(alloc_t) + block->size;
        }

        double fragmentation = 0;
        if (free_mem > 0) {
            fragmentation = (double)(free_mem - largest_free) / free_mem * 100.0;
        }

        printf("\n========== STATISTICS ==========\n");
        printf("Pool size: %zu bytes\n", total_pool_sz);
        printf("Used:      %zu bytes\n", used);
        printf("Free:      %zu bytes (Blocks: %zu)\n", free_mem, free_blocks);
        printf("Fragmentation: %.2f %%\n", fragmentation);
        printf("Allocations: %zu\n", total_allocations);
        printf("Frees:       %zu\n", total_frees);
        printf("================================\n\n");
    }

    void DrawMemoryMap() {
        const int cells = 64;
        printf("\nMemory Map:\n[");

        for (int i = 0; i < cells; i++) {
            size_t position = i * total_pool_sz / cells;
            uintptr_t curr = reinterpret_cast<uintptr_t>(start_pool);
            bool cell_printed = false;

            while (curr < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
                alloc_t* block = reinterpret_cast<alloc_t*>(curr);
                size_t start = (char*)block - (char*)start_pool;
                size_t end = start + sizeof(alloc_t) + block->size;

                if (position >= start && position < end) {
                    printf("%c", (block->status == 1) ? '#' : '.');
                    cell_printed = true;
                    break;
                }
                curr += sizeof(alloc_t) + block->size;
            }
            if (!cell_printed) printf(".");
        }
        printf("]\n\n");
    }

public:
    AdvancedMemoryManager(size_t pool_size) {
        if (pool_size % 8 != 0) pool_size += (8 - (pool_size % 8));
        total_pool_sz = pool_size;

        start_pool = std::malloc(total_pool_sz);
        if (!start_pool) {
            std::cerr << "Крах системи: немає пам'яті!\n";
            std::exit(1);
        }

        alloc_t* first_block = reinterpret_cast<alloc_t*>(start_pool);
        first_block->signature = 0xDEADBEEF;
        first_block->size = total_pool_sz - sizeof(alloc_t);
        first_block->prev_size = 0;
        first_block->status = 0;

        uintptr_t data_addr = reinterpret_cast<uintptr_t>(start_pool) + sizeof(alloc_t);
        freeTree.insert_block(first_block->size, data_addr);
    }

    ~AdvancedMemoryManager() {
        std::free(start_pool);
        std::cout << "[Система] Пам'ять повернута ОС.\n";
    }

    void* my_malloc(size_t bytes_requested) {
        if (!bytes_requested) return nullptr;

        size_t original_request = bytes_requested;
        if (bytes_requested % 8 != 0) bytes_requested += (8 - (bytes_requested % 8));

        node_t* n = freeTree.search_fit(bytes_requested);
        if (!n) {
            std::cout << "!!! Немає підходящого блоку для " << bytes_requested << " байт !!!\n";
            return nullptr;
        }

        alloc_t* curr_block = reinterpret_cast<alloc_t*>(n->val - sizeof(alloc_t));
        size_t old_total_size = n->key;
        uintptr_t old_val = n->val;

        freeTree.remove_block(old_total_size, old_val);
        curr_block->status = 1;
        size_t original_block_size = curr_block->size;
        curr_block->size = bytes_requested;

        if (original_block_size > bytes_requested + sizeof(alloc_t) + 8) {
            alloc_t* next = reinterpret_cast<alloc_t*>(n->val + bytes_requested);
            next->signature = 0xDEADBEEF;
            next->prev_size = bytes_requested;
            next->status = 0;
            next->size = original_block_size - bytes_requested - sizeof(alloc_t);

            alloc_t* future_next = reinterpret_cast<alloc_t*>(reinterpret_cast<uintptr_t>(next) + sizeof(alloc_t) + next->size);
            if (reinterpret_cast<uintptr_t>(future_next) < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
                future_next->prev_size = next->size;
            }

            uintptr_t next_data_addr = reinterpret_cast<uintptr_t>(next) + sizeof(alloc_t);
            freeTree.insert_block(next->size, next_data_addr);
        } else {
            curr_block->size = original_block_size;
            alloc_t* next = reinterpret_cast<alloc_t*>(reinterpret_cast<uintptr_t>(curr_block) + sizeof(alloc_t) + curr_block->size);
            if (reinterpret_cast<uintptr_t>(next) < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
                next->prev_size = curr_block->size;
            }
        }

        std::memset(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(curr_block) + sizeof(alloc_t)), 0, curr_block->size);
        curr_block->signature = 0xDEADBEEF;

        total_allocations++;
        printf("[ALLOC] %zu bytes -> offset %zu\n",
               original_request, (char*)curr_block - (char*)start_pool);

        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(curr_block) + sizeof(alloc_t));
    }

    void my_free(void* mem) {
        if (!mem) return;

        alloc_t* target = reinterpret_cast<alloc_t*>(reinterpret_cast<uintptr_t>(mem) - sizeof(alloc_t));
        if (target->signature != 0xDEADBEEF) {
            std::cout << "Помилка: Спроба звільнити невалідну адресу!\n";
            return;
        }
        if (target->status == 0) {
            std::cout << "Помилка: Блок уже вільний (Double Free)!\n";
            return;
        }

        total_frees++;
        printf("[FREE ] offset %zu size %zu\n",
               (char*)target - (char*)start_pool, target->size);

        target->status = 0;

        if (target->prev_size > 0) {
            alloc_t* left = reinterpret_cast<alloc_t*>(reinterpret_cast<uintptr_t>(target) - sizeof(alloc_t) - target->prev_size);
            if (left->signature == 0xDEADBEEF && left->status == 0 && left->size == target->prev_size) {
                uintptr_t left_data = reinterpret_cast<uintptr_t>(left) + sizeof(alloc_t);
                freeTree.remove_block(left->size, left_data);
                left->size += sizeof(alloc_t) + target->size;
                target = left;
            }
        }

        alloc_t* right = reinterpret_cast<alloc_t*>(reinterpret_cast<uintptr_t>(target) + sizeof(alloc_t) + target->size);
        if (reinterpret_cast<uintptr_t>(right) < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
            if (right->prev_size && right->status == 0 && right->signature == 0xDEADBEEF) {
                uintptr_t right_data = reinterpret_cast<uintptr_t>(right) + sizeof(alloc_t);
                freeTree.remove_block(right->size, right_data);
                target->size += sizeof(alloc_t) + right->size;
            }
        }

        alloc_t* far_right = reinterpret_cast<alloc_t*>(reinterpret_cast<uintptr_t>(target) + sizeof(alloc_t) + target->size);
        if (reinterpret_cast<uintptr_t>(far_right) < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
            far_right->prev_size = target->size;
        }

        uintptr_t alloc_data = reinterpret_cast<uintptr_t>(target) + sizeof(alloc_t);
        freeTree.insert_block(target->size, alloc_data);
    }

    void ReportLeaks() {
        printf("\n========= MEMORY LEAKS =========\n");
        bool found = false;

        uintptr_t curr = reinterpret_cast<uintptr_t>(start_pool);
        while (curr < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
            alloc_t* block = reinterpret_cast<alloc_t*>(curr);
            if (block->status == 1) {
                found = true;
                size_t addr = (char*)block - (char*)start_pool;
                printf("Leak -> offset=%zu size=%zu bytes\n", addr, block->size);
            }
            curr += sizeof(alloc_t) + block->size;
        }

        if (!found)
            printf("No leaks detected.\n");
        printf("================================\n");
    }

    void ПоточнийСтан() {
        printf("\n");
        printf("========================================\n");
        printf("          MEMORY MANAGER STATE\n");
        printf("========================================\n");

        uintptr_t curr = reinterpret_cast<uintptr_t>(start_pool);
        int id = 0;

        while (curr < reinterpret_cast<uintptr_t>(start_pool) + total_pool_sz) {
            alloc_t* block = reinterpret_cast<alloc_t*>(curr);
            size_t offset = (char*)block - (char*)start_pool;

            printf("Block %-2d | Offset %-4zu | Size %-4zu | %s\n",
                   id++, offset, block->size, (block->status == 1) ? "USED" : "FREE");

            curr += sizeof(alloc_t) + block->size;
        }

        DrawMemoryMap();
        PrintStatistics();
        printf("========================================\n\n");
    }
};

// Автоматичне перемикання консолі Windows на UTF-8 (65001) при старті програми
#ifdef _WIN32
    struct WinCP { WinCP() { system("chcp 65001 > nul"); } } win_cp_init;
#endif

// Структура для красивого виводу звіту про файли
struct AllocationInfo {
    std::string filename;
    size_t size;
    void* address;
};

int main() {
    // 1. Ініціалізуємо менеджер з пулом на 4 Кілобайти (4096 байт)
    printf("Ініціалізація менеджера пам'яті під роботу з файлами...\n");
    AdvancedMemoryManager manager(4096);
    manager.ПоточнийСтан();

    std::vector<AllocationInfo> active_files;
    std::string target_file = "document.txt"; // Ім'я файлу для читання

    // 2. Відкриваємо файл у бінарному режимі
    std::ifstream file(target_file, std::ios::binary);

    if (!file.is_open()) {
        printf("[УВАГА] Не вдалося відкрити файл '%s'!\n", target_file.c_str());
        printf("Будь ласка, створи текстовий файл '%s' у папці 'output' поруч із .exe файлом програми і перезапусти її.\n\n", target_file.c_str());
        return 1;
    }

    // 3. Визначаємо точний розмір файлу на диску в байтах
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    printf("[ФАЙЛ] Успішно знайдено файл '%s' розміром %zu байт.\n", target_file.c_str(), fileSize);

    // 4. Просимо наш алокатор виділити блок пам'яті під цей розмір
    void* file_ptr = manager.my_malloc(fileSize);

    if (file_ptr != nullptr) {
        // 5. Читаємо весь файл безпосередньо в буфер нашого менеджера пам'яті
        file.read(reinterpret_cast<char*>(file_ptr), fileSize);
        file.close();

        // Записуємо дані у вектор для звіту
        active_files.push_back({target_file, fileSize, file_ptr});
        printf("[СИСТЕМА] Байти файлу завантажені у виділену менеджером пам'ять.\n");

        // 6. Виведемо вміст файлу прямо з нашого буфера, щоб перевірити, що все зчиталося
        printf("\n--- ВМІСТ ФАЙЛУ З НАШОЇ КУЧІ ---\n");
        for (size_t i = 0; i < fileSize; i++) {
            putchar(reinterpret_cast<char*>(file_ptr)[i]);
        }
        printf("\n--------------------------------\n");
    } else {
        printf("[ПОМИЛКА] Менеджеру не вистачило пам'яті для завантаження файлу.\n");
        file.close();
    }

    // 7. Дивимося, як змінився стан пам'яті (карта і блоки) після завантаження файлу
    manager.ПоточнийСтан();

    // Вивід красивого списку завантажених файлів
    printf("============= ОБ'ЄКТИ В ПАМ'ЯТІ =============\n");
    for (const auto& info : active_files) {
        printf("%-15s -> %zu bytes (Address: %p)\n", info.filename.c_str(), info.size, info.address);
    }
    printf("=============================================\n\n");

    // 8. Звільняємо пам'ять з-під файлу
    printf("[FREE] Очищення пам'яті...\n");
    if (file_ptr) {
        manager.my_free(file_ptr);
    }

    // 9. Фінальна перевірка на витоки пам'яті
    manager.ReportLeaks();

    return 0;
}
