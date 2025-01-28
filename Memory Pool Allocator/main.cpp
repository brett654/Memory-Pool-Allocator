#include <iostream>
#include <atomic>
#include <cstdint>
#include <thread>

struct Block {
    Block* next;
};

struct Node {
    int data;
    Node* next; //The next member in a node holds the address of the next node in the linked list or nullptr if there are no further nodes.

    Node(int value) : data(value), next(nullptr) {}
};

class Spinlock {
private:
    std::atomic_flag lock = ATOMIC_FLAG_INIT;

public:
    void lockPool() {
        while (lock.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void unlockPool() {
        lock.clear(std::memory_order_release);
    }
};

void* alignPointer(void* ptr, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        throw std::invalid_argument("Alignment must be a power of two");
    }

    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    size_t misalignment = p % alignment;
    size_t adjustment = (alignment - misalignment) % alignment;

    uintptr_t aligned = p + adjustment;

    return reinterpret_cast<void*>(aligned);
}

class MemoryPool {
private:
    Spinlock spinlock;
    Block* freeList; //pointer to the first available block
    void* poolStart; // Pointer to the start of the entire memory pool
    size_t blockSize;
    size_t totalBlocks;
    size_t usedBlocks; // Track the number of blocks currently in use
    size_t alignment;
public:
    MemoryPool(size_t blocksize, size_t totalBlocks, size_t alignment = alignof(std::max_align_t)) {
        this->alignment = alignment;
        initializeMemoryPool(blocksize, totalBlocks);
    }

    ~MemoryPool() {
        destroyMemoryPool();
    }

    Block* allocateBlock() {
        spinlock.lockPool();

        if (!freeList) {
            resizePool();
        }

        if (!freeList) {
            spinlock.unlockPool();
            return nullptr;
        }

        Block* allocateBlock = freeList;
        freeList = freeList->next;
        usedBlocks++;
        spinlock.unlockPool();

        return allocateBlock;
    }

    void deallocateBlock(Block* block) {
        spinlock.lockPool();
        block->next = freeList;
        freeList = block;
        spinlock.unlockPool();
    }

    void reset() {
        freeList = static_cast<Block*>(poolStart);
        usedBlocks = 0;
    }
private:
    void initializeMemoryPool(size_t blockSize, size_t totalBlocks) {
        this->blockSize = blockSize;
        this->totalBlocks = totalBlocks;
        this->alignment = alignment;

        void* rawPool = operator new(blockSize * totalBlocks + alignment - 1);

        this->poolStart = alignPointer(rawPool, alignment);

        this->freeList = static_cast<Block*>(this->poolStart);

        Block* currentBlock = this->freeList;

        for (size_t i = 1; i < totalBlocks; i++) {
            currentBlock->next = reinterpret_cast<Block*>(reinterpret_cast<char*>(currentBlock) + blockSize);
            currentBlock = currentBlock->next;
        }
        currentBlock->next = nullptr;
    } 

    void resizePool() {
        size_t newSize = totalBlocks + (totalBlocks / 2);

        void* newRawPool = operator new(blockSize * newSize + alignment - 1);
        if (!newRawPool) {
            throw std::runtime_error("Memory allocation failed during resize.");
        }
        void* newPoolStart = alignPointer(newRawPool, alignment);

        Block* newFreeList = static_cast<Block*>(newPoolStart);
        Block* currentBlock = newFreeList;

        for (size_t i = 1; i < newSize; i++) {
            currentBlock->next = reinterpret_cast<Block*>(reinterpret_cast<char*>(currentBlock) + blockSize);
            currentBlock = currentBlock->next;
        }

        currentBlock->next = nullptr;

        currentBlock->next = freeList;
        freeList = newFreeList;
        totalBlocks = newSize;

        std::cout << "Memory pool resized. New size: " << totalBlocks << " blocks." << std::endl;
    }

    void destroyMemoryPool() {
        operator delete(this->poolStart);
        this->poolStart = nullptr;
        this->freeList = nullptr;
    }  
};

class SingleLinkedList {
private:
    Spinlock spinlock;
    Node* head; // Pointer to the head node
    MemoryPool* pool;
public:
    SingleLinkedList(MemoryPool* memoryPool) : head(nullptr), pool(memoryPool) {}

    int getLength() {
        int length = 0;
        Node* current = head;

        while (current != nullptr) {
            length++;
            current = current->next;
        }
        return length;
    }

    void insert(int value) {
        Block* block = pool->allocateBlock();

        if (!block) {
            std::cout << "Memory pool is full. Cannot allocate new node." << std::endl;
            return;
        }

        Node* newNode = new (block) Node(value); // Placement new to construct Node in the allocated block

        if (head == nullptr) { // If the list is empty
        head = newNode; // Set the head to the new node
        }

        else {
            Node* current = head;

            while (current->next != nullptr) {
                current = current->next; //move to the next node
            }
            current->next = newNode; // Link the last node to the new node
        }
    }

    void remove(int value) {
        if (head == nullptr) return;

        Node* current = head;
        Node* previous = nullptr;

        while (current != nullptr && current->data != value) {
            previous = current;
            current = current->next;
        }

        if (current == nullptr) {
            std::cout << "Value " << value << " not found in the list." << std::endl;
            return;
        }

        if (current == head) { // if the value to be removed is the head node
            head = head->next; //move head to next node
        }
        else {
            previous->next = current->next; // Bypass the current node
        }

        pool->deallocateBlock(reinterpret_cast<Block*>(current));
        std::cout << "Value " << value << " removed from the list." << std::endl;
    }
    
    Node* merge(Node* left, Node* right) {
        if (!left) return right;
        if (!right) return left;

        if (left->data <= right ->data) {
            left->next = merge(left->next, right);
            return left;
        }
        else {
            right->next = merge(left, right->next);
            return right;
        }
    }

    Node* getMiddle() {
        return getMiddle(head);
    }

    Node* getMiddle(Node* node) {
        if (!node || !node->next) {
            return node;
        }
        Node* slow = node;
        Node* fast = node->next;

        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
        }

        return slow;
    }

    void mergeSort() {
        head = mergeSort(head); // Start sorting from the head
    }

    Node* mergeSort(Node* node) {
        if (!node || !node->next) {
            return node; // Base case: If list is empty or has one node
        }

        Node* middle = getMiddle(node); // Get the middle node
        Node* secondHalf = middle->next; // Split the list into two halves
        middle->next = nullptr; // Break the list into two halves

        // Recursively sort both halves
        Node* leftSorted = mergeSort(node);
        Node* rightSorted = mergeSort(secondHalf);

        return merge(leftSorted, rightSorted); // Merge sorted halves
    }

    void clear() {
        spinlock.lockPool();
        Node* current = head;

        while (current != nullptr) {
            Node* temp = current;
            current = current->next;
            pool->deallocateBlock(reinterpret_cast<Block*>(temp));
        }
        head = nullptr;
        spinlock.unlockPool();
    }

    void display() const {
        Node* current = head;

        while (current != nullptr) {
            std::cout << current->data << " ";
            current = current->next; 
        }
        std::cout << std::endl;
    }

    ~SingleLinkedList() {
        spinlock.lockPool();
        Node* current = head;

        while (current != nullptr) {
            Node* temp = current;
            current = current->next; // move to the next node leaving behind the last current that's why we store it in temp
            temp->~Node();
            pool->deallocateBlock(reinterpret_cast<Block*>(temp));
        }
        spinlock.unlockPool();
    }
};

int main() {
    MemoryPool pool(sizeof(Node), 10); //32 bytes and 10 blocks

    SingleLinkedList list(&pool);

    for (int i = 1; i <= 20; ++i) {
        list.insert(i);
    }

    std::cout << "Linked list: ";
    list.display();

    list.remove(2);
    std::cout << "Linked list: ";
    list.display();

    return 0;
}

