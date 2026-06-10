#include <iostream>
#include <cstdlib>

// Структура чанка пам'яті. Зробив як у методичці, тільки додав прапорець занятості
struct MemBlock {
    size_t size;        // корисний розмір (без врахування цієї структури)
    bool is_busy;       // 1 - зайнято, 0 - вільно
    MemBlock* next_ptr; // лінк на наступний шматок в кучі
};

class MyMemoryManager {
private:
    void* start_pool;     // початок виділеної області
    size_t total_pool_sz; // скільки всього байт випросили у системи
    MemBlock* head;       // голова нашого списку

public:
    // Конструктор. Виділяємо загальну кучу
    MyMemoryManager(size_t pool_size) {
        // Округлення до 8 байт по-простому, бо макроси з інтернету не працювали
        if (pool_size % 8 != 0) {
            pool_size = pool_size + (8 - (pool_size % 8));
        }
        
        total_pool_sz = pool_size;
        start_pool = malloc(total_pool_sz);
        
        if (start_pool == NULL) {
            printf("!!! КРАШ: Система не дала пам'яті !!!\n");
            exit(1); // якщо не виділилось, то далі ловити нічого
        }

        // Ініціалізуємо перший великий шматок
        head = (MemBlock*)start_pool;
        head->size = total_pool_sz - sizeof(MemBlock);
        head->is_busy = false;
        head->next_ptr = NULL;
    }

    // Деструктор. Чистим за собою
    ~MyMemoryManager() {
        free(start_pool);
        printf("[системне] пам'ять успішно повернута ОС.\n");
    }

    // Мій аналог malloc. Шукає перший підходящий (First Fit)
    void* my_malloc(size_t bytes_requested) {
        if (bytes_requested == 0) return NULL;

        // Вирівнювання запиту по 8 байт (вимога проца, щоб не лагало)
        if (bytes_requested % 8 != 0) {
            bytes_requested = bytes_requested + (8 - (bytes_requested % 8));
        }

        MemBlock* curr = head;
        while (curr != NULL) {
            // Перевіряємо чи вільний і чи влізе
            if (!curr->is_busy && curr->size >= bytes_requested) {
                
                // Спліттінг (розпилювання). Чи є сенс різати блок?
                // Треба щоб лишилось місце хоча б під структуру MemBlock + 8 байт для даних
                size_t needed_space_for_split = bytes_requested + sizeof(MemBlock) + 8;
                
                if (curr->size >= needed_space_for_split) {
                    // Рахуємо адресу нового заголовка (через char* щоб побайтово зсунутись)
                    char* new_block_addr = (char*)curr + sizeof(MemBlock) + bytes_requested;
                    MemBlock* next_block = (MemBlock*)new_block_addr;

                    // Налаштовуємо новий вільний чанк
                    next_block->size = curr->size - bytes_requested - sizeof(MemBlock);
                    next_block->is_busy = false;
                    next_block->next_ptr = curr->next_ptr;

                    // Обрізаємо поточний шматок
                    curr->size = bytes_requested;
                    curr->next_ptr = next_block;
                }

                curr->is_busy = true; // тепер зайнято
                
                // Повертаємо поінтер на початок даних (зсув вперед на розмір структури)
                return (void*)((char*)curr + sizeof(MemBlock));
            }
            curr = curr->next_ptr; // йдемо далі по ланцюгу
        }

        printf("!!! УВАГА: Немає підходящого місця під %zu байт !!!\n", bytes_requested);
        return NULL; 
    }

    // Мій аналог free
    void my_free(void* ptr) {
        if (ptr == NULL) return;

        // Вертаємось назад на розмір структури, щоб прочитати заголовок
        MemBlock* target = (MemBlock*)((char*)ptr - sizeof(MemBlock));
        
        // Костиль для перевірки double free (якщо вже false - значить баг у коді)
        if (!target->is_busy) {
            printf("ПОМИЛКА: Спроба видалити вже вільний блок за адресою %p!\n", ptr);
            return;
        }
        
        target->is_busy = false; // Звільнили!

        // Коалесценція (склеювання сусідніх порожніх блоків)
        // TODO: Переписати на двосвязний список, бо цей прохід O(N) гальмує кучу
        MemBlock* curr = head;
        while (curr != NULL && curr->next_ptr != NULL) {
            if (!curr->is_busy && !curr->next_ptr->is_busy) {
                // Плюсуємо розмір структури сусіда і його чистий розмір
                curr->size += sizeof(MemBlock) + curr->next_ptr->size;
                // Викидаємо сусіда зі списку, бо ми його поглинули
                curr->next_ptr = curr->next_ptr->next_ptr;
            } else {
                curr = curr->next_ptr;
            }
        }
    }

    // Дебаг-функція для курсової (виводить карту пам'яті)
    void ПоточнийСтан() {
        MemBlock* curr = head;
        int i = 0;
        printf("------- КАРТА КУЧІ -------\n");
        while (curr != NULL) {
            // Рахуємо відносне зміщення від старту для зручності
            size_t smeshenie = (char*)curr - (char*)start_pool;
            printf("Чанк #%d | Зміщення: +%zu б. | Розмір даних: %zu б. | Статус: %s\n", 
                   i++, smeshenie, curr->size, curr->is_busy ? "ЗАЙНЯТО [X]" : "ВІЛЬНО [ ]");
            curr = curr->next_ptr;
        }
        printf("--------------------------\n\n");
    }
};

int main() {
    // Створюємо менеджер на 1024 байти для демонстрації викладачу
    printf("Ініціалізація нашого менеджера...\n");
    MyMemoryManager manager(1024);
    manager.ПоточнийСтан();

    printf("Тест 1: Виділяємо блоки під дані курсової (101, 200, 50 байт)\n");
    void* a1 = manager.my_malloc(101); // Округлить до 104
    void* a2 = manager.my_malloc(200);
    void* a3 = manager.my_malloc(50);  // Округлить до 56
    manager.ПоточнийСтан();

    printf("Тест 2: Видаляємо середній блок (200 байт), робимо дірку\n");
    manager.my_free(a2);
    manager.ПоточнийСтан();

    printf("Тест 3: Пробуємо виділити 60 байт (має спрацювати First Fit і відрізати від дірки)\n");
    void* a4 = manager.my_malloc(60); // Округлить до 64
    manager.ПоточнийСтан();

    printf("Тест 4: Очищаємо все інше (має спрацювати склеювання в один великий блок)\n");
    manager.my_free(a1);
    manager.my_free(a3);
    manager.my_free(a4);
    manager.ПоточнийСтан();

    return 0;
}