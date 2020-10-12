#ifndef ARRAY_QUEUE
#define ARRAY_QUEUE

#include <iostream>

using namespace std;

template<class T, int size = 100>
class ArrayQueue {
public:
    ArrayQueue() +{
        first = last = -1;
    }

    void enqueue(T);

    T dequeue();

    bool isFull() {
        // 判断队列是否为空的时候有，要分成边界和非边界两种情况讨论
        return first == 0 && last == size -1 || first == last + 1;
    }

    bool isEmpty() {
        return first == -1;
    }
private:
    /*
        数组构建队列的时候一般都是设计成循环队列
        一般都会包含有两个索引，一个指向数组的头部，一个指向数组的尾部
    */
    int first, last;
    T storage[size];
};

template<class T, int size>
void ArrayQueue<T, size>::enqueue(T el) {
    if (!isFull()) {
        if (last == size - 1 || last == -1) {
            storage[0] = el;
            last = 0;

            if (first == -1) {
                first = 0;
            } else {
                storage[++last] = el;
            }
        } else {
            cout << "Full queue!\n";
        }
    }
}

template<class T, int size>
T ArrayQueue<T, size>::dequeue() {
    T tmp;
    tmp = storage[first];

    if (first == last) {
        last = first = -1;
    } else if (first == size - 1) {     // 从头部取数据的时候要判断first是否在尾部
        first = 0;
    } else {
        first++;
    }
    return tmp;
}
#endif