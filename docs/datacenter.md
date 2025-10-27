# node数据

有两个数据

1. 节点坐标数组
2. 用户定义id与数组index映射
3. 内部index到用户定义id的映射

```cpp
std::vector<double> node_coordinates;         // [x1, y1, z1, x2, y2, z2, ...]
std::unordered_map<NodeID, size_t> node_id_to_index; // 外部ID -> 内部逻辑索引
std::vector<NodeID> node_index_to_id;       // 内部逻辑索引 -> 外部ID
```

核心的节点数组结构：

```
[x1,y1,z1,x2,y2,z2,...,xn,yn,zn]
```

第index节点的x y z是node_coordinates[3\*index], node_coordinates[3\*index+1], node_coordinates[3\*index+2]

因此，index是0base连续的数，0,1,2, ...,n，所以node_index_to_id不需要map，直接一个数组就行

输入文件结构如下

```
*node begin
# node id, x, y, z
1,1,1,1
2,1,0,1
3,1,1,0
4,1,0,0
5,0,1,1
6,0,0,1
7,0,1,0
8,0,0,0
* node end
```

