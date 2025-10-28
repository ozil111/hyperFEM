# node数据

有3个数据

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
*node end
```

# element连接数据

首先需要有一个几何type

命名规则，采用三位数，首位是维度，第二三位是节点数，比如四节点壳就是204

| typeid | 单元类型       |
| ------ | -------------- |
| 102    | 普通梁单元     |
| 103    | 三节点梁单元   |
| 203    | 平面三角形单元 |
| 204    | 平面四边形单元 |
| 208    | 二阶平面四边形 |
| 304    | 四面体         |
| 306    | Penta          |
| 308    | 八节点六面体   |
| 310    | 二阶四面体     |
| 320    | 20节点六面体   |

输入格式如下

```
*element begin
#element id,element geometry type,node ids,..,
1,308,5,6,8,7,1,2,4,3
*element end
```

需要存储的数据如下

1. 根据单元id能找到单元type
2. 根据单元id能找到node id

需要以下数据

```cpp
std::vector<int> element_types;               // [type_e1, type_e2, ...] 存储每个单元的类型ID
std::vector<int> element_connectivity;        // 所有单元的节点ID连续存储
std::vector<size_t> element_offsets;          // 每个单元的节点列表在 connectivity 中的起始位置
std::unordered_map<ElementID, size_t> element_id_to_index; // 外部ID -> 内部索引
std::vector<ElementID> element_index_to_id;   // 内部索引 -> 外部ID 
```

