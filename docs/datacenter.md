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

## **有限元单元库节点顺序规范**

### 1. 概述

本文档定义了自定义有限元单元库中各类单元的节点连接顺序。遵循统一的节点顺序对于正确构建单元面、保证拓扑关系查找的准确性以及后续计算（如计算单元法向）至关重要。

- **二维单元**：节点顺序通常遵循**逆时针**方向，以确保单元法线方向（根据右手定则）朝向屏幕外。
- **三维单元**：面的节点顺序同样遵循**逆时针**方向（从单元外部看），以确保面法线朝向单元外部。
- **二阶单元**：节点列表的顺序为：**先列出所有角点，再列出所有中点**。在进行拓扑分析（即查找共享面）时，仅使用角点来定义面。

------

### 2. 一维单元 (1D Elements)

一维单元没有“面”的概念，它们的连接性体现在共享节点上。

#### **单元类型 102: 2节点线性梁 (Linear Beam)**

```
1----------2
```

- **节点列表**: `[node_1, node_2]`

#### **单元类型 103: 3节点二阶梁 (Quadratic Beam)**

```
1-----3-----2
```

- **角点**: 1, 2
- **中点**: 3
- **节点列表**: `[node_1, node_2, node_3]`

------

### 3. 二维单元 (2D Elements)

二维单元的面即为其边。

#### **单元类型 203: 3节点线性三角形 (Linear Triangle)**

```
      3
     / \
    /   \
   1-----2
```

- **节点列表**: `[node_1, node_2, node_3]`
- **边 (Faces)**:
  - 边 1: `[1, 2]`
  - 边 2: `[2, 3]`
  - 边 3: `[3, 1]`

#### **单元类型 204: 4节点线性四边形 (Linear Quadrilateral)**

```
   4-------3
   |       |
   |       |
   1-------2
```

- **节点列表**: `[node_1, node_2, node_3, node_4]`
- **边 (Faces)**:
  - 边 1: `[1, 2]`
  - 边 2: `[2, 3]`
  - 边 3: `[3, 4]`
  - 边 4: `[4, 1]`

#### **单元类型 208: 8节点二阶四边形 (Quadratic Quadrilateral)**

```
   4---7---3
   |       |
   8       6
   |       |
   1---5---2
```

- **角点**: 1, 2, 3, 4
- **中点**: 5, 6, 7, 8
- **节点列表**: `[node_1, node_2, node_3, node_4, node_5, node_6, node_7, node_8]`
- **拓扑边 (Topological Faces - 仅使用角点)**:
  - 边 1: `[1, 2]`
  - 边 2: `[2, 3]`
  - 边 3: `[3, 4]`
  - 边 4: `[4, 1]`

------

### 4. 三维单元 (3D Elements)

#### **单元类型 304: 4节点线性四面体 (Linear Tetrahedron)**	

```
      4
     /|\
    / | \
   /  |  \
  1---|---3
   \  |  /
    \ | /
     \|/
      2
```

- **节点列表**: `[node_1, node_2, node_3, node_4]`
- **面 (Faces)**:
  - 面 1 (底面): `[1, 2, 3]`
  - 面 2: `[1, 4, 2]` (注意顺序，原代码为1,3,1，修正为1,4,2更合理)
  - 面 3: `[2, 4, 3]` (修正)
  - 面 4: `[3, 4, 1]` (修正)

*(注: `get_faces_from_element` 函数中四面体的面定义顺序经过调整以符合标准的右手定则，确保法线向外)*

#### **单元类型 306: 6节点线性楔形体 (Linear Wedge/Penta)**

```
      6
     / \
    /   \
   4-----5
   |  3  |
   | / \ |
   |/   \|
   1-----2
```

- **节点列表**: `[node_1, node_2, node_3, node_4, node_5, node_6]`
- **面 (Faces)**:
  - 面 1 (底面, 三角形): `[1, 2, 3]`
  - 面 2 (顶面, 三角形): `[4, 5, 6]`
  - 面 3 (侧面, 四边形): `[1, 2, 5, 4]`
  - 面 4 (侧面, 四边形): `[2, 3, 6, 5]`
  - 面 5 (侧面, 四边形): `[3, 1, 4, 6]`

#### **单元类型 308: 8节点线性六面体 (Linear Hexahedron/Brick)**

```
      8-------7
     /|      /|
    / |     / |
   5-------6  |
   |  4----|--3
   | /     | /
   |/      |/
   1-------2
```

- **节点列表**: `[node_1, node_2, node_3, node_4, node_5, node_6, node_7, node_8]`
- **面 (Faces)**:
  - 面 1 (底面, Z-): `[1, 2, 3, 4]`
  - 面 2 (顶面, Z+): `[5, 6, 7, 8]`
  - 面 3 (前面, Y-): `[1, 2, 6, 5]`
  - 面 4 (后面, Y+): `[4, 3, 7, 8]`
  - 面 5 (左面, X-): `[1, 4, 8, 5]`
  - 面 6 (右面, X+): `[2, 3, 7, 6]`

#### **单元类型 310: 10节点二阶四面体 (Quadratic Tetrahedron)**

- **角点**: 1, 2, 3, 4
- **中点**: 5, 6, 7, 8, 9, 10
- **节点列表**: `[node_1, ..., node_4, node_5, ..., node_10]`
- **拓扑面 (Topological Faces - 仅使用角点)**:
  - 面 1: `[1, 2, 3]`
  - 面 2: `[1, 4, 2]`
  - 面 3: `[2, 4, 3]`
  - 面 4: `[3, 4, 1]`

#### **单元类型 320: 20节点二阶六面体 (Quadratic Hexahedron/Brick)**

- **角点**: 1, 2, 3, 4, 5, 6, 7, 8
- **中点**: 9 到 20
- **节点列表**: `[node_1, ..., node_8, node_9, ..., node_20]`
- **拓扑面 (Topological Faces - 仅使用角点)**:
  - 面 1 (底面): `[1, 2, 3, 4]`
  - 面 2 (顶面): `[5, 6, 7, 8]`
  - 面 3 (前面): `[1, 2, 6, 5]`
  - 面 4 (后面): `[4, 3, 7, 8]`
  - 面 5 (左面): `[1, 4, 8, 5]`
  - 面 6 (右面): `[2, 3, 7, 6]`

## 拓扑结构数据

我们定义一个连续网格体为一个body，采用洪水填充算法来寻找body。这样做的目的是为了让复杂模型一目了然

为了实现高效的拓扑查询（如查找相邻单元、识别连续体），hyperFEM引入了一套独立于核心 `Mesh` 数据之外的拓扑数据结构。这套结构遵循数据与逻辑分离的设计思想，所有数据都包含在 `TopologyData` 结构体中，而所有操作则由 `TopologySystems` 类来执行。

#### 1.1 核心设计理念

- **性能优先**: 在所有内部关系映射中，使用 `std::vector` 并通过内部索引 (`size_t`) 进行访问，以实现 $O(1)$ 的查找效率。
- **稳定性**: 在需要创建唯一标识符（如面的定义）时，使用用户提供的外部ID（`NodeID`），因为它们是固定不变的。
- **数据分离**: `TopologyData` 仅持有对原始 `Mesh` 数据的引用，不进行任何拷贝，保证了数据的一致性和内存效率。

#### 1.2 `TopologyData` 结构详解

```cpp
struct TopologyData {
    // --- 核心拓扑实体 ---
    std::vector<FaceKey> faces;
    std::unordered_map<FaceKey, FaceID, VectorHasher> face_key_to_id;

    // --- 关系映射表 ---
    std::vector<std::vector<FaceID>> element_to_faces;
    std::vector<std::vector<ElementIndex>> face_to_elements;

    // --- 连续体数据 ---
    std::vector<BodyID> element_to_body;
    std::unordered_map<BodyID, std::vector<ElementIndex>> body_to_elements;

    // --- 辅助数据 ---
    const Mesh& source_mesh;
};
```

- **`FaceKey`**: `std::vector<NodeID>`
  - 一个面的唯一标识。它由组成该面的所有**角点**节点的**外部ID**（`NodeID`）构成，并经过排序。
  - **设计原因**: 使用外部ID和排序保证了无论单元定义中节点的顺序如何，同一个面总能得到相同的`FaceKey`。只使用角点（忽略中点）确保了线性单元和二阶单元能够正确识别共享面。
- **`faces`**: `std::vector<FaceKey>`
  - 存储程序中所有唯一面的列表。这个向量的**索引**就是面的内部ID (`FaceID`)。
  - 例: `faces[5]` 将返回第5个面的 `FaceKey`。
- **`face_key_to_id`**: `std::unordered_map<FaceKey, FaceID, ...>`
  - 一个从 `FaceKey` 到其内部 `FaceID` 的快速查找表，用于在拓扑构建时高效地判断一个面是新面还是已存在的面。
- **`element_to_faces`**: `std::vector<std::vector<FaceID>>`
  - **单元到面**的正向查找表。
  - `element_to_faces[elem_idx]` 返回一个包含所有属于该单元（由其内部索引 `elem_idx` 标识）的 `FaceID` 的列表。
- **`face_to_elements`**: `std::vector<std::vector<ElementIndex>>`
  - **面到单元**的反向查找表，是拓扑分析的**核心**。
  - `face_to_elements[face_id]` 返回一个共享该面（由 `face_id` 标识）的所有单元的**内部索引** (`ElementIndex`) 列表。
  - 通过检查此列表的大小，可以判断面的类型：
    - `size() == 1`: 这是一个**边界（Boundary）面**，位于模型的表面。
    - `size() == 2`: 这是一个**内部（Internal）面**，连接了两个相邻单元。
- **`element_to_body`**: `std::vector<BodyID>`
  - 记录每个单元所属的连续网格体。
  - `element_to_body[elem_idx]` 返回该单元所属的 `BodyID`。
- **`body_to_elements`**: `std::unordered_map<BodyID, std::vector<ElementIndex>>`
  - 记录每个连续网格体包含的所有单元。
  - `body_to_elements[body_id]` 返回一个包含该连续体所有单元**内部索引**的列表。

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

# hyperFEM 交互式程序使用说明

hyperFEM 提供两种操作模式：**批处理模式**和**交互模式**，以适应不同的使用场景。

#### 2.1 模式选择

程序的行为由启动时是否提供 `--input-file` (或 `-i`) 参数决定。

- **批处理模式 (Batch Mode)**

  - **触发方式**: 启动程序时提供输入文件路径。

  - **用途**: 自动化脚本、文件格式转换、无人值守任务。

  - **示例**:

    ```Bash
    # 仅解析并导出一个新文件
    hyperFEM_app --input-file case/model.xfem --output-file case/output.xfem
    ```

- **交互模式 (Interactive Mode)**

  - **触发方式**: 启动程序时不提供任何输入文件路径。

  - **用途**: 模型探索、交互式调试、分步分析与简化。

  - **示例**:

    ```Bash
    # 直接启动进入交互式控制台
    hyperFEM_app
    ```

#### 2.2 交互模式命令参考

启动后，您将看到 `hyperFEM>` 提示符，可以输入以下命令：

**`import <file_path>`**

- **功能**: 加载并解析一个 `.xfem` 格式的网格文件到内存中。如果已有模型被加载，此命令会先清空旧数据。

- **示例**:

  ```Bash
  hyperFEM> import D:\cases\complex_assembly.xfem
  ```

**`build_topology`**

- **功能**: 基于当前加载的网格，执行完整的拓扑分析。此命令会：

  1. 提取所有唯一的面，并建立单元与面之间的双向查找关系。
  2. 执行洪水填充算法，识别并划分所有的连续网格体（Bodies）。

- **前置条件**: 必须先使用 `import` 命令加载一个网格。

- **示例**:

  ```Bash
  hyperFEM> build_topology
  ```

**`list_bodies`**

- **功能**: 显示当前模型中所有已识别的连续网格体及其包含的单元数量。

- **前置条件**: 必须先运行 `build_topology`。

- **示例**:

  ```Bash
  hyperFEM> list_bodies
  ```

**`info`**

- **功能**: 显示当前会话的状态摘要，包括：

  - 已加载网格的节点、单元和集合数量。
  - 已构建拓扑的唯一面和连续体数量。

- **示例**:

  ```Bash
  hyperFEM> info
  ```

**`save <file_path>`**

- **功能**: 将当前内存中的 `Mesh` 数据导出到一个新的 `.xfem` 文件。注意：此命令只保存原始网格数据，不保存拓扑分析结果。

- **前置条件**: 必须先使用 `import` 命令加载一个网格。

- **示例**:

  ```Bash
  hyperFEM> save D:\cases\simplified_model.xfem
  ```

**`help`**

- **功能**: 显示所有可用命令的列表。

- **示例**:

  ```Bash
  hyperFEM> help
  ```

**`quit` 或 `exit`**

- **功能**: 退出 hyperFEM 交互式会话。

- **示例**:

  ```bash
  hyperFEM> quit
  ```

#### 2.3 典型工作流程 (交互模式)

一个典型的模型调试与分析流程如下：

1. **启动程序**: `hyperFEM_app`
2. **加载模型**: `import path/to/your/model.xfem`
3. **构建拓扑**: `build_topology`
4. **检查模型**: `info` (快速查看摘要)
5. **分析连续体**: `list_bodies` (查看模型由几个独立的零件构成)
6. **(未来功能)**: 执行选择、简化等操作...
7. **保存结果**: `save path/to/your/output.xfem`
8. **退出程序**: `exit`

# 函数曲线



# 材料

首先需要有一个材料type

命名规则，采用三位数，首位是大类，第二三位是小类

第一位规则

| 第一位 | 大类   |
| ------ | ------ |
| 0      | 弹性   |
| 1      | 超弹   |
| 2      | 粘弹   |
| 3      | 弹塑性 |

具体材料类型如下

| typeid | 材料               |
| ------ | ------------------ |
| 1      | 各向同性线弹性材料 |
| 101    | polynomial         |
| 102    | reduced polynomial |
| 103    | ogden              |

未来持续添加

具体到每一种材料需要输入的参数需要单独定义

对于材料，特别是超弹材料，有一个特点就是参数个数是不固定的，不能像几何单元那样固定每种材料有多少参数。而且材料一般来说不会像单元那么多，可以不必像材料节点那样维护一个统一大数组节省内存

关键是要根据mid找到type id，然后根据type id调用对应的材料程序

## mat1

密度、弹性模量、泊松比

输入格式如下

```
*material begin
#typeid,mid,rho,E,nu
1,1,1e5,2e9,0.3
*material end
```

$rho>0$

$-1\leq nu\leq0.5$

$E>0$

## polynomial

超弹性材料都支持两种定义方式，方式一是直接输入材料参数，方式2是实验曲线拟合。拟合功能引用外部python脚本，是我自己写的一个专门用于超弹材料拟合的库

直接输入格式如下

```
*material begin
#typeid,mid,order,0,ci0...,c0i...,di...
101,1,1,0,c10,c01,d1
101,1,2,0,c10,c20,c01,c02,d1,d2
*material end
```

曲线拟合输入格式如下

```
*material begin
# typeid,mid,order,1,unixial test data flag,bixial test data flag,planar test data flag,volumeteric test data flag,unixial func id...,bixial func id...,planar func id...,volumetric func id...,nu
101,1,1,1,1,1,1,0,1,2,3,0.49
*material end
```

## reduced polynomial

直接输入格式如下

```
*material begin
#typeid,mid,order,0,ci0...,di...
102,1,1,0,c10,d1
102,1,2,0,c10,c20,d1,d2
*material end
```

曲线拟合输入格式如下

```
*material begin
# typeid,mid,order,1,unixial test data flag,bixial test data flag,planar test data flag,volumeteric test data flag,unixial func id...,bixial func id...,planar func id...,volumetric func id...,nu
101,1,1,1,2,2,0,0,1,2,3,4,0.49
*material end
```

## odgen

直接输入格式如下

```
*material begin
#typeid,mid,order,0,alphai...,mui...,di...
103,1,1,0,alpha1,mu1,d1
103,1,2,0,alpha2,alpha2,mu1,mu2,d1,d2
*material end
```

实验曲线输入格式如下

```
*material begin
# typeid,mid,order,1,unixial test data flag,bixial test data flag,planar test data flag,volumeteric test data flag,unixial func id...,bixial func id...,planar func id...,volumetric func id...,nu
101,1,1,1,1,1,1,0,1,2,3,0.49
*material end
```

