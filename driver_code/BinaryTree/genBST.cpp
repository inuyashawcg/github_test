#ifndef BINARY_SEARCH_TREE
#include "genBST.h"
#endif

template<class T>
T* BST<T>::search(BSTNode<T>* p, const T& el) const {
    while(0 != p) {
        if (el == p -> el) {
            return &p -> el;
        } else if (el < p -> el) {
            p = p -> left;
        } else {
            p = p -> right;
        }
    }
    return 0;
}

template<class T>
void BST<T>::breadthFirst() {
    Queue<BSTNode<T>*> queue;
    BSTNode<T> *p = root;

    if (0 != p) {
        queue.enqueue(p);

        while(!queue.empty()) {
            p = queue.dequeue();
            visit(p);

            if (p -> left != 0 ) {
                queue.enqueue(p -> left);
            }

            if (p -> right != 0) {
                queue.enqueue(p -> right);
            }
        }
    }
}

/**
 * 利用递归实现二叉树的遍历，但是如果二叉树特别大的话，这种采用两次
 * 递归的方法就变得相当不实用了，会给程序造成极大的负担
 */ 
template<class T>
void BST<T>::inorder(BSTNode<T>* P) {
    if (p != 0) {
        inorder(p -> left);
        visit(p);
        inorder(p -> right);
    }
}

template<class T>
void BST<T>::preorder(BSTNode<T>* p) {
    if (p != 0) {
        visit(p);
        preorder(p -> left);
        preorder(p -> right);
    }
}

template<class T>
void BST<T>::postorder(BSTNode<T>* p) {
    if (p != 0) {
        preorder(p -> left);
        preorder(p -> right);
        visit(p);
    }
}

/*
    尝试一下用非递归的方式实现一下二叉树的遍历，借助堆栈
    采用递归的方式，执行的顺序是先左后右，堆栈方式的话就是先右后左
    这种方式也可以看到，执行的操作其实跟递归差不多，每次while循环中都进行了四次调用：
    两次push，一次pop，一次visit，效率也没有太大的提升
*/
template<class T>
void BST<T>::iterativePreorder() {
    Stack<BSTNode<T>*> travStack;
    BSTNode<T> *p = root;

    if (p != 0) {
        travStack.push(p);
        while(!travStack.empty()) {
            p = travStack.pop();
            visit(p);

            if (p -> right != 0) {
                travStack.push(p -> right);
            }

            if (p -> left != 0) {
                travStack.push(p -> left);
            }
        }
    }
}

template <class T>
void BST<T>::iterativePostorder() {
    Stack<BSTNode<T>*> travStack;
    BSTNode<T>* p = root, *q = root;

    while (p != 0) {
        
        for (; p -> left != 0; p = p -> left) {
            travStack.push(p);
        }

        /*
            通过上述for循环之后，一定可以保证该节点的左子树是空，
            后边就要去判断右子树
        */ 
        while (p -> right == 0 || p -> right == q) {
            visit(p);
            q = p;
            if (travStack.empty()) {
                return;
            }
            p = travStack.pop();
        }

        travStack.push(p);
        p = p -> right;
        // q的主要作用就是记录p的执行步骤，防止对节点的第三次push
    }
}

template<class T>
void BST<T>::iterativeInorder() {
    Stack<BSTNode<T>*> travStack;
    BSTNode<T> *p = root;
    while (p != 0) {
        while (p != 0) {
            if (p -> right) {
                travStack.push(p -> right);
            }
            travStack.push(p);
            p = p -> left;
        }//

        p = travStack.pop();
        
        while (!travStack.empty() && p -> right == 0) {
            visit(p);
            p = travStack.pop();
        }

        visit(p);
        if (!travStack.empty()) {
            p = travStack.pop();
        } else {
            p = 0;
        }
    }//
}
