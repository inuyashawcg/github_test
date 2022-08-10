#ifndef _SINGLE_LIST_H_
#define _SINGLE_LIST_H_

class IntSingleListNode {
public:
  IntSingleListNode() = default;
  IntSingleListNode(int Info);
  ~IntSingleListNode();

public:
  void setNodeInfo(int Info);

private:
  int NodeInfo;
  IntSingleListNode* NextNode;
};

#endif // _SINGLE_LIST_H_