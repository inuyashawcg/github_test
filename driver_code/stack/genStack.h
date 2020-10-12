#ifndef STACK
#define STACK

#include <vector>
using namespace std;

template<class T, int capacity = 30>
class Stack {
public:
    Stack() {
        pool.reserve(capacity);
    }

    ~Stack() {}

    void clear() {
        pool.clear();
    }

    bool isEmpty() const {
        return pool.empty();
    }

    T& topEL() {
        return pool.back();
    }

    T pop() {
        T el = pool.back();
        pool.pop_back();
        return el;
    }

    void push(const T& el) {
        pool.push_back(el);
    }
private:
    vector<T> pool;
};

#endif