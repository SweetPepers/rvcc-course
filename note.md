shell 别有事没事加空格

fprintf(stderr, "%*s", Pos, "");   打印了pos個空格
### 4 token流 构造

```c
// 为每个终结符都设置种类来表示
typedef enum
{
    TK_PUNCT, // 操作符如： + - 
    TK_NUM,   // 数字
    TK_EOF,   // 文件终止符，即文件的最后
} TokenKind;

// 终结符结构体
typedef struct Token Token;
struct Token
{
    TokenKind Kind; // 种类
    Token *Next;    // 指向下一终结符
    int Val;        // 值
    char *Loc;      // 在解析的字符串内的位置
    int Len;        // 长度
};

```
跳过空格啥的

### 5 支持 * / () 也就是优先级
简单的加减乘除的抽象语法树  加上括号  表示优先级
![](./picture/%E5%9B%9B%E5%88%99%E8%BF%90%E7%AE%97%E8%AF%AD%E6%B3%95%E6%A0%91.jpg)

```c
// AST节点种类
typedef enum {
  ND_ADD, // +
  ND_SUB, // -
  ND_MUL, // *
  ND_DIV, // /
  ND_NUM, // (整型 int)  数字
} NodeKind;


// AST中二叉树节点
typedef struct Node Node;
struct Node {
  NodeKind Kind; // 节点种类
  Node *LHS;     // 左部，left-hand side
  Node *RHS;     // 右部，right-hand side
  int Val;       // 存储ND_NUM种类的值
};

// expr = mul ("+" mul | "-" mul)*
// mul = primary ("*" primary | "/" primary)*
// primary = "(" expr ")" | num
static Node *expr(Token **Rest, Token *Tok);
static Node *mul(Token **Rest, Token *Tok);
static Node *primary(Token **Rest, Token *Tok);

static void genExpr(Node *Nd) 
// 递归将最右节点入栈  解析完左子树之后弹出
```

### 6 一元运算符
一元运算符优先级高于乘除

```c
// expr = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num
```

### 7 == != > < >= <=
优先级
```c

// 优先级
// expr = equality
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num
```

判断操作符占几个字节(1 or 2)
```c
// 判断Str是否以SubStr开头
static bool startsWith(char *Str, char *SubStr) {
  // 比较LHS和RHS的N个字符是否相等
  return strncmp(Str, SubStr, strlen(SubStr)) == 0;
}

// 读取操作符
static int readPunct(char *Ptr) {
  // 判断2字节的操作符
  if (startsWith(Ptr, "==") || startsWith(Ptr, "!=") || startsWith(Ptr, "<=") ||
      startsWith(Ptr, ">="))
    return 2;

  // 判断1字节的操作符
  return ispunct(*Ptr) ? 1 : 0;
}
```


### 8 代码重构, 将main分割为多个文件
- `codegen.c` 语义分析与代码生成  通过栈操作解析语法树生成代码
- `parse.c`   生成AST, 根据token序列生成抽象语法树   语法分析
- `tokenize.c`将输入字符串解析为一个一个token   词法分析


### 9 支持;分隔语句

- rvcc.h 里面
添加NodeKind::ND_EXPR_STMT, // 表达式语句 : 表示是一个语句

- parse.h 添加新语法规则
语法规则
```c
// program = stmt*
// stmt = exprStmt
// exprStmt = expr ";"
// expr = equality
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | num

```
program由多个表达式语句构成, 采用链表存储多个语句
```c
struct Node {
  // new!!
  Node *Next;    // 下一节点，指代下一语句
};
```
- codegen.c
生成多个语句, 为每个语句生成代码


### 10 支持单字母本地变量
- rvcc.h
  - add TokenKind::TK_IDENT  标记符
  - add NodeKind::ND_ASSIGN  赋值 NodeKind::ND_VAR 变量
  - add Node::char Name 变量名字

- tokenize.c
a-z 自动识别为变量

- parse.c
```c
// 语法
// program = stmt*
// stmt = exprStmt
// exprStmt = expr ";"
// expr = assign                                     new
// assign = equality ("=" assign)?                   new
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = "(" expr ")" | ident | num             new
```
primary() 
```c
  // ident
  if (Tok->Kind == TK_IDENT){
    Node *Nd = newVarNode(*(Tok->Loc));  // 用字符位置
    *Rest = Tok->Next;
    return Nd;
  }
```


- codegen.c
入口函数初始化栈, 自动在栈上生成24个变量 a-z, 并存储a的地址fp
`Offset = (Nd->Name - 'a' + 1) * 8; `
之后变量的地址就是 
`addi a0, fp, %d (-Offset) `
最后释放


### 11 支持多字母本地变量
- rvcc.h
定义变量的结构体
```c
// 本地变量
typedef struct Obj Obj;
struct Obj {
  Obj *Next;  // 指向下一对象
  char *Name; // 变量名
  int Offset; // fp的偏移量
};

// 不再以node为入口, 函数由语法树及其附属结构(变量表)组成 
// 函数  
typedef struct Function Function;
struct Function {
  Node *Body;    // 函数体
  Obj *Locals;   // 本地变量
  int StackSize; // 栈大小
};

```
- tokennize.c
判断变量名是否合法

```c
// 解析标记符  [a-zA-Z_][a-zA-Z0-9_]*
    if (isIdent1(*P)){
      char *Start = P;
      do{
        ++P;
      }while(isIdent2(*P));


// 判断标记符的首字母规则
// [a-zA-Z_]
static bool isIdent1(char C) {
  // a-z与A-Z在ASCII中不相连，所以需要分别判断
  return ('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z') || C == '_';
}

// 判断标记符的非首字母的规则
// [a-zA-Z0-9_]
static bool isIdent2(char C) { return isIdent1(C) || ('0' <= C && C <= '9'); }

```

- parse.c
因为变量由 char升级为 Obj, 做一些相应的修改
```c
// 维持一个链表结构存储本地变量名
Obj *Locals;
// 通过名称，查找一个本地变量
static Obj *findVar(Token *Tok);

// 新变量
static Node *newVarNode(Obj *Var);
// 在链表中新增一个变量
static Obj *newLVar(char *Name);
```

- codegen.c

`static void assignLVarOffsets(Function *Prog) `
根据链表长度计算初始化栈的大小, 并对齐16位`Prog->StackSize = alignTo(Offset, 16)`, 修改初始化栈大小


### 12 支持return语句
目前成果 
```c
foo2=70; bar4=4;return foo2+bar4;

编译结果:
  .globl main
main:
  addi sp, sp, -8
  sd fp, 0(sp)
  mv fp, sp
  addi sp, sp, -16
  addi a0, fp, -16
  addi sp, sp, -8
  sd a0, 0(sp)
  li a0, 70
  ld a1, 0(sp)
  addi sp, sp, 8
  sd a0, 0(a1)
  addi a0, fp, -8
  addi sp, sp, -8
  sd a0, 0(sp)
  li a0, 4
  ld a1, 0(sp)
  addi sp, sp, 8
  sd a0, 0(a1)
  addi a0, fp, -8
  ld a0, 0(a0)
  addi sp, sp, -8
  sd a0, 0(sp)
  addi a0, fp, -16
  ld a0, 0(a0)
  ld a1, 0(sp)
  addi sp, sp, 8
  add a0, a0, a1
  j .L.return
.L.return:
  mv sp, fp
  ld fp, 0(sp)
  addi sp, sp, 8
  ret
```

- rvcc.h
  - TokenKind::TK_KEYWORD
  - NodeKind::ND_RETURN
- tokenize.c
  void convertKeywords(Token *Tok) : 扫描token, 将 字符为`return`的token的Kind换为TK_KEYWORD
- parse.c
  语法更新
  `stmt = "return" expr ";" | exprStmt`
  为returntoken建立单叉树

- codegen.c
  现在stmt由return语句或者exprstmt组成, 写一个switch分别翻译, 并在epilogue上加入 `.L.return`的跳转标签

### 13 支持{...}代码块


- rcvv.h
  - NodeKind::ND_BLOCK
  - Node::Body // 代码块

- parse.c
// program = "{" compoundStmt
// compoundStmt = stmt* "}"
// stmt = "return" expr ";" | "{" compoundStmt | exprStmt
// exprStmt = expr ";"

  parse()  为{}, 必须以{开始, 到}结束, 多个stmt用链表存储
- codegen.c
  ```c
  genStmt()
   switch(Nd->Kind){
    // 生成代码块，遍历代码块的语句链表
    case ND_BLOCK: 从Nd->Body开始一个一个按照stmt解析
    ...
    }
  ```

### 14 支持空语句 ;;;
// exprStmt = expr? ";"
- parse.c
```c
  // ";"
  if (equal(Tok, ";")){
    *Rest = Tok->Next;
    return newNode(ND_BLOCK);
  }
```