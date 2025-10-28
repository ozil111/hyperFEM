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

极致优化：缓存最后一次的查找结果

尽管如此，我们确实有一种几乎零成本的方法来解决每次读取单元都需要查找注册表的问题。这个方法利用了网格文件中一个非常常见的特性：**相同类型的单元通常会连续出现在文件中**。

例如，一个文件里可能有成千上万个`308`单元，然后是成千上万个`304`单元。

我们可以利用这个局部性原理，缓存上一次的查找结果。

**实现思路：** 在你的解析器类或函数中，维护两个静态或成员变量：

- `cachedTypeId`: 缓存的最后一个单元类型ID。
- `cachedProperties`: 指向该类型属性的指针或引用。


这种缓存策略的优势：

- **性能提升巨大**：假设文件中有100万个单元，但只有5种不同的类型，并且每种类型的单元都聚集在一起。那么，`elementRegistry.find()` 这个真正的哈希查找操作总共只会被执行**5次**，而不是100万次！后续的999,995次访问都只是一个简单的整数比较 (`typeId != cachedTypeId`)，这个开销可以忽略不计。
- **代码改动极小**：你只需要在原来的查找逻辑外加一个 `if` 判断和两个缓存变量，核心逻辑不变，保持了代码的清晰和可维护性。
- **完全兼容**：即使文件中的单元类型是完全随机、交错排列的，这个方法也不会出错，最坏情况下降级为每次都查找，和没有缓存时一样。

# set

set分为node set和element set
需要实现以下需求

1. 根据set id找到node id或者element id

因此我们必须区分node set和element set，而且和节点单元不同，set可能有很多，需要随时定义，不像节点单元可以维护一个统一的大数组

我在这里想的是统一set，即存储的时候每个set就是一个map<int, vector>

最后就是增加一个vector\<string\> 来存储string

还需要一个map来根据name找到id

输入格式如下

```
*eleset begin
#id,name,contents...
1,all,1
*eleset end

*nodeset begin
#id,name,contents...
1,fix,3,4,7,8
2,load,1,2,5,6
*nodeset end
```

# 换行语法

我们规定：如果一行（在去除注释和前后空格后）以逗号结尾，那么它表示下一行是当前逻辑行的延续。

**示例：**

一个拥有20个节点的单元，如果不换行会非常长：

```
# 原始长行
101, 320, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
```

使用行连续符，可以写成这样，更清晰：

```
# 使用行连续符的多行
101, 320, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,  # <-- 注意这里的注释
           11, 12, 13, 14, 15, 16,        #     会被正确处理
           17, 18, 19, 20
```

我们的解析器需要能将后面这段多行文本合并成与第一段等价的单个字符串进行处理。
