#include "rvcc.h"

// 语义分析与代码生成
static void genStmt(Node *Nd);
static void genExpr(Node *Nd);

// 输出文件
static FILE *OutputFile;

// 输出字符串到目标文件并换行
static void printLn(char *Fmt, ...) {
  va_list VA;

  va_start(VA, Fmt);
  vfprintf(OutputFile ,Fmt, VA);
  va_end(VA);

  fprintf(OutputFile, "\n");
}

// 代码段计数
static int count(void) {
  static int I = 1;
  return I++;
}

// 对齐到Align的整数倍
int alignTo(int N, int Align) {
  if (Align == 0){
    error("error type");
  }
  // (0,Align]返回Align
  return (N + Align - 1) / Align * Align;
}


// 记录栈深度
static int Depth;

// // 用于函数参数的寄存器们
// static char *ArgReg[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};

// 当前的函数
static Obj *CurrentFn;


// 压栈，将结果临时压入栈中备用
// sp为栈指针，栈反向向下增长，64位下，8个字节为一个单位，所以sp-8
// 当前栈指针的地址就是sp，将a0的值压入栈
// 不使用寄存器存储的原因是因为需要存储的值的数量是变化的。
static void push(void) {
  printLn("  addi sp, sp, -8");
  printLn("  sd a0, 0(sp)");
  Depth++;
}

// 弹栈，将sp指向的地址的值，弹出到a1
static void pop(int Reg) {
  printLn("  ld a%d, 0(sp)", Reg);
  printLn("  addi sp, sp, 8");
  Depth--;
}

// 对于浮点类型进行压栈
static void pushF(void) {
  printLn("  # 压栈, 将fa0的值存入栈顶");
  printLn("  addi sp, sp, -8");
  printLn("  fsd fa0, 0(sp)");
  Depth++;
}

// 对于浮点类型进行弹栈
static void popF(int Reg) {
  printLn("  # 弹栈, 将栈顶的值存入fa%d", Reg);
  printLn("  fld fa%d, 0(sp)", Reg);
  printLn("  addi sp, sp, 8");
  Depth--;
}



// 计算给定节点的绝对地址
// 如果报错，说明节点不在内存中
static void genAddr(Node *Nd) {
  switch (Nd->Kind){
    case ND_VAR:
      if(Nd->Var->IsLocal){
        // 偏移量是相对于fp的    
        printLn("  # 获取局部变量%s的栈内地址为%d(fp)", Nd->Var->Name, Nd->Var->Offset);
        printLn("  li t0, %d", Nd->Var->Offset);
        printLn("  add a0, fp, t0");
      }else{ // 全局变量 or 函数
        printLn("  # 获取%s%s的地址", Nd->Ty->Kind == TY_FUNC ? "函数" : "全局变量", Nd->Var->Name);
        printLn("  la a0, %s", Nd->Var->Name);  // 全局变量存放在符号表中, data段
      }
      return;
    case ND_DEREF:
      // 解引用*
      genExpr(Nd->LHS);
      return;
    // 逗号
    case ND_COMMA:
      genExpr(Nd->LHS);
      genAddr(Nd->RHS);
      return;
    // 结构体成员
    case ND_MEMBER:
      genAddr(Nd->LHS);
      printLn("  # 计算成员变量的地址偏移量");
      printLn("  addi a0, a0, %d", Nd->Mem->Offset);
      return;
    default:
      break;
  }

  errorTok(Nd->Tok, "not an lvalue");
}

// 加载a0指向的值
static void load(Type *Ty) {
  switch (Ty->Kind) {
  case TY_ARRAY:
  case TY_STRUCT:
  case TY_UNION:
  case TY_FUNC:
    return;
  case TY_FLOAT:
    printLn("  # 访问a0中存放的地址,取得的值存入fa0");
    printLn("  flw fa0, 0(a0)");
    return;
  case TY_DOUBLE:
    printLn("  # 访问a0中存放的地址,取得的值存入fa0");
    printLn("  fld fa0, 0(a0)");
    return;
  default:
    break;
  }

  // 添加无符号类型的后缀u
  char *Suffix = Ty->IsUnsigned ? "u" : "";

  printLn("  # 读取a0中存放的地址, 得到的值存入a0");
  if(Ty->Size == 1){
    printLn("  lb%s a0, 0(a0)", Suffix);
  }else if (Ty->Size == 2){
    printLn("  lh%s a0, 0(a0)", Suffix);
  }else if (Ty->Size == 4){
    printLn("  lw%s a0, 0(a0)", Suffix);
  }else{
    printLn("  ld a0, 0(a0)");
  }
}

// 将a0存入栈顶值(为一个地址)
static void store(Type *Ty) {
  pop(1);
  
  switch (Ty->Kind) {
  case TY_STRUCT:
  case TY_UNION:
    printLn("  # asign to %s", Ty->Kind == TY_STRUCT ? "struct" : "union");
    for (int I = 0; I < Ty->Size; ++I) {
      printLn("  lb a2, %d(a0)", I);
      printLn("  sb a2, %d(a1)", I);
    }
    return;
  case TY_FLOAT:
    printLn("  # 将fa0的值, 写入到a1中存放的地址");
    printLn("  fsw fa0, 0(a1)");
    return;
  case TY_DOUBLE:
    printLn("  # 将fa0的值, 写入到a1中存放的地址");
    printLn("  fsd fa0, 0(a1)");
    return;
  default:
    break;
  }

  printLn("  # 将a0的值, 写入到a1中存放的地址");
  if (Ty->Size == 1){
    printLn("  sb a0, 0(a1)");
  }else if (Ty->Size == 2){
    printLn("  sh a0, 0(a1)");
  }else if (Ty->Size == 4){
    printLn("  sw a0, 0(a1)");
  }else{
    printLn("  sd a0, 0(a1)");
  }
};

// 与0进行比较，不等于0则置1
static void notZero(Type *Ty) {
  switch (Ty->Kind) {
  case TY_FLOAT:
    printLn("  # 判断fa1是否不为0, 为0置0, 非0置1");
    printLn("  fmv.s.x fa1, zero");
    printLn("  feq.s a0, fa0, fa1");
    printLn("  xori a0, a0, 1");
    return;
  case TY_DOUBLE:
    printLn("  # 判断fa1是否不为0, 为0置0, 非0置1");
    printLn("  fmv.d.x fa1, zero");
    printLn("  feq.d a0, fa0, fa1");
    printLn("  xori a0, a0, 1");
    return;
  default:
    return;
  }
}

// 类型枚举
enum { I8, I16, I32, I64, U8, U16, U32, U64, F32, F64}; 

// 获取类型对应的枚举值
static int getTypeId(Type *Ty) {
  switch (Ty->Kind) {
  case TY_CHAR:
    return Ty->IsUnsigned? U8:I8;
  case TY_SHORT:
    return Ty->IsUnsigned? U16:I16;
  case TY_INT:
    return Ty->IsUnsigned? U32:I32;
  case TY_LONG:
    return Ty->IsUnsigned? U64:I64;
  case TY_FLOAT:
    return F32;
  case TY_DOUBLE:
    return F64;
  default:
    return U64;
  }
}

// 类型映射表. 高64-n位置零
// 先逻辑左移N位，再算术右移N位，就实现了将64位有符号数转换为64-N位的有符号数
static char i64i8[] = "  # 转换为i8类型\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56";
static char i64i16[] = "  # 转换为i16类型\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48";
static char i64i32[] = "  # 转换为i32类型\n"
                       "  slli a0, a0, 32\n"
                       "  srai a0, a0, 32";

// 先逻辑左移N位，再逻辑右移N位，就实现了将64位无符号数转换为64-N位的无符号数
static char i64u8[] = "  # 转换为u8类型\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56";
static char i64u16[] = "  # 转换为u16类型\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48";
static char i64u32[] = "  # 转换为u32类型\n"
                       "  slli a0, a0, 32\n"
                       "  srli a0, a0, 32";

// 有符号整型转换为浮点数
static char i64f32[] = "  # i64转换为f32类型\n"
                       "  fcvt.s.l fa0, a0";
static char i64f64[] = "  # i64转换为f64类型\n"
                       "  fcvt.d.l fa0, a0";

// 无符号整型转换为浮点数
static char u64f32[] = "  # u64转换为f32类型\n"
                       "  fcvt.s.lu fa0, a0";
static char u64f64[] = "  # u64转换为f64类型\n"
                       "  fcvt.d.lu fa0, a0";

// 单精度浮点数转换为整型
static char f32i8[] = "  # f32转换为i8类型\n"
                      "  fcvt.w.s a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56\n";
static char f32i16[] = "  # f32转换为i16类型\n"
                       "  fcvt.w.s a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48\n";
static char f32i32[] = "  # f32转换为i32类型\n"
                       "  fcvt.w.s a0, fa0, rtz";
static char f32i64[] = "  # f32转换为i64类型\n"
                       "  fcvt.l.s a0, fa0, rtz";

// 无符号整型转换为无符号浮点数
static char f32u8[] = "  # f32转换为u8类型\n"
                      "  fcvt.wu.s a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56\n";
static char f32u16[] = "  # f32转换为u16类型\n"
                       "  fcvt.wu.s a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48\n";
static char f32u32[] = "  # f32转换为u32类型\n"
                       "  fcvt.wu.s a0, fa0, rtz";
static char f32u64[] = "  # f32转换为u64类型\n"
                       "  fcvt.lu.s a0, fa0, rtz";

// 单精度转换为双精度浮点数
static char f32f64[] = "  # f32转换为f64类型\n"
                       "  fcvt.d.s fa0, fa0";

// 双精度浮点数转换为整型
static char f64i8[] = "  # f64转换为i8类型\n"
                      "  fcvt.w.d a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srai a0, a0, 56\n";
static char f64i16[] = "  # f64转换为i16类型\n"
                       "  fcvt.w.d a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srai a0, a0, 48\n";
static char f64i32[] = "  # f64转换为i32类型\n"
                       "  fcvt.w.d a0, fa0, rtz";
static char f64i64[] = "  # f64转换为i64类型\n"
                       "  fcvt.l.d a0, fa0, rtz";

// 双精度浮点数转换为无符号整型
static char f64u8[] = "  # f64转换为u8类型\n"
                      "  fcvt.wu.d a0, fa0, rtz\n"
                      "  slli a0, a0, 56\n"
                      "  srli a0, a0, 56\n";
static char f64u16[] = "  # f64转换为u16类型\n"
                       "  fcvt.wu.d a0, fa0, rtz\n"
                       "  slli a0, a0, 48\n"
                       "  srli a0, a0, 48\n";
static char f64u32[] = "  # f64转换为u32类型\n"
                       "  fcvt.wu.d a0, fa0, rtz";
static char f64u64[] = "  # f64转换为u64类型\n"
                       "  fcvt.lu.d a0, fa0, rtz";

// 双精度转换为单精度浮点数
static char f64f32[] = "  # f64转换为f32类型\n"
                       "  fcvt.s.d fa0, fa0";                      

// 所有类型转换表
static char *castTable[10][10] = {
    // clang-format off

    // 转换到 cast to
    // {i8,  i16,    i32,    i64,    u8,    u16,    u32,    u64,    f32,    f64}
    {NULL,   NULL,   NULL,   NULL,   i64u8, NULL,   NULL,   NULL,   i64f32, i64f64}, // 从i8转换
    {i64i8,  NULL,   NULL,   NULL,   i64u8, i64u16, NULL,   NULL,   i64f32, i64f64}, // 从i16转换
    {i64i8,  i64i16, NULL,   NULL,   i64u8, i64u16, i64u32, NULL,   i64f32, i64f64}, // 从i32转换
    {i64i8,  i64i16, i64i32, NULL,   i64u8, i64u16, i64u32, NULL,   i64f32, i64f64}, // 从i64转换

    {i64i8,  NULL,   NULL,   NULL,   NULL,  NULL,   NULL,   NULL,   u64f32, u64f64}, // 从u8转换
    {i64i8,  i64i16, NULL,   NULL,   i64u8, NULL,   NULL,   NULL,   u64f32, u64f64}, // 从u16转换
    {i64i8,  i64i16, i64i32, NULL,   i64u8, i64u16, NULL,   NULL,   u64f32, u64f64}, // 从u32转换
    {i64i8,  i64i16, i64i32, NULL,   i64u8, i64u16, i64u32, NULL,   u64f32, u64f64}, // 从u64转换

    {f32i8,  f32i16, f32i32, f32i64, f32u8, f32u16, f32u32, f32u64, NULL,   f32f64}, // 从f32转换
    {f64i8,  f64i16, f64i32, f64i64, f64u8, f64u16, f64u32, f64u64, f64f32, NULL  }, // 从f64转换

    // clang-format on
};

// 类型转换
static void cast(Type *From, Type *To) {
  if (To->Kind == TY_VOID)
    return;
  if (To->Kind == TY_BOOL) {
    notZero(From);
    printLn("  # 转为bool类型:为0置0,非0置1");
    printLn("  snez a0, a0");
    return;
  }

  // 获取类型的枚举值
  int T1 = getTypeId(From);
  int T2 = getTypeId(To);
  if (castTable[T1][T2]) {
    printLn("  # 转换函数");
    printLn("%s", castTable[T1][T2]);
  }
}

// 将函数实参计算后压入栈中
static void pushArgs(Node *Args) {
  // 参数为空直接返回
  if (!Args)
    return;

  // 递归到最后一个实参进行
  pushArgs(Args->Next);

  printLn("\n  # 对%s表达式进行计算, 然后压栈",
          isFloNum(Args->Ty) ? "浮点" : "整型");
  // 计算出表达式
  genExpr(Args);
  // 根据表达式结果的类型进行压栈
  if (isFloNum(Args->Ty)) {
    pushF();
  } else {
    push();
  }
  printLn("  # 结束压栈");
}

// 生成表达式
static void genExpr(Node *Nd) {
  // .loc 文件编号 行号
  printLn("  .loc 1 %d", Nd->Tok->LineNo);
  // 生成各个根节点
  switch (Nd->Kind) {
  // 空表达式
  case ND_NULL_EXPR:
    return;
  // 加载数字到a0
  // float和uint32、double和uint64 共用一份内存空间
   case ND_NUM: {
    union {
      float F32;
      double F64;
      uint32_t U32;
      uint64_t U64;
    } U;

    switch (Nd->Ty->Kind) {
    case TY_FLOAT:
      U.F32 = Nd->FVal;
      printLn("  # 将a0转换到float类型值为%f的fa0中", Nd->FVal);
      printLn("  li a0, %u  # float %f", U.U32, Nd->FVal);
      printLn("  fmv.w.x fa0, a0");
      return;
    case TY_DOUBLE:
      printLn("  # 将a0转换到double类型值为%f的fa0中", Nd->FVal);
      U.F64 = Nd->FVal;
      printLn("  li a0, %lu  # double %f", U.U64, Nd->FVal);
      printLn("  fmv.d.x fa0, a0");
      return;
    default:
      printLn("  # 将%ld加载到a0中", Nd->Val);
      printLn("  li a0, %ld", Nd->Val);
      return;
    }
  }
  // 对寄存器取反
  case ND_NEG:
    genExpr(Nd->LHS);
    switch (Nd->Ty->Kind) {
    case TY_FLOAT:
      printLn("  # 对float类型的fa0值进行取反");
      printLn("  fneg.s fa0, fa0");
      return;
    case TY_DOUBLE:
      printLn("  # 对double类型的fa0值进行取反");
      printLn("  fneg.d fa0, fa0");
      return;
    default:
      // neg a0, a0是sub a0, x0, a0的别名, 即a0=0-a0
      printLn("  # 对a0值进行取反");
      printLn("  neg a0, a0");
      // printLn("  sub a0, x0, a0");
      return;
    }

    return;
  // 非运算
  case ND_NOT:
    genExpr(Nd->LHS);
    notZero(Nd->LHS->Ty);
    printLn("  # 非运算");
    // a0=0则置1，否则为0
    printLn("  seqz a0, a0");
    return;
  // 逻辑与
  case ND_LOGAND: {
    int C = count();
    printLn("\n# =====逻辑与%d===============", C);
    genExpr(Nd->LHS);
    notZero(Nd->LHS->Ty);
    // 判断是否为短路操作
    printLn("  # 左部短路操作判断, 为0则跳转");
    printLn("  beqz a0, .L.false.%d", C);
    genExpr(Nd->RHS);
    notZero(Nd->RHS->Ty);
    printLn("  # 右部判断, 为0则跳转");
    printLn("  beqz a0, .L.false.%d", C);
    printLn("  li a0, 1");
    printLn("  j .L.end.%d", C);
    printLn(".L.false.%d:", C);
    printLn("  li a0, 0");
    printLn(".L.end.%d:", C);
    return;
  }
  // 逻辑或
  case ND_LOGOR: {
    int C = count();
    printLn("\n# =====逻辑或%d===============", C);
    genExpr(Nd->LHS);
    notZero(Nd->LHS->Ty);
    // 判断是否为短路操作
    printLn("  # 左部短路操作判断, 不为0则跳转");
    printLn("  bnez a0, .L.true.%d", C);
    genExpr(Nd->RHS);
    notZero(Nd->RHS->Ty);
    printLn("  # 右部判断, 不为0则跳转");
    printLn("  bnez a0, .L.true.%d", C);
    printLn("  li a0, 0");
    printLn("  j .L.end.%d", C);
    printLn(".L.true.%d:", C);
    printLn("  li a0, 1");
    printLn(".L.end.%d:", C);
    return;
  }
  // 按位取非运算
  case ND_BITNOT:
    genExpr(Nd->LHS);
    printLn("  # 按位取反");
    // 这里的 not a0, a0 为 xori a0, a0, -1 的伪码
    // printLn("  not a0, a0");
    printLn("  xori a0, a0, -1");
    return;
  // 变量
  case ND_VAR:
  case ND_MEMBER:
    // 计算出变量的地址，然后存入a0
    genAddr(Nd);
    // 访问a0地址中存储的数据，存入到a0当中
    load(Nd->Ty);
    return;
  // 赋值
  case ND_ASSIGN:
    // 左部是左值，保存值到的地址
    genAddr(Nd->LHS);
    push();
    // 右部是右值，为表达式的值
    genExpr(Nd->RHS);
    store(Nd->Ty);
    return;
  // 解引用 *   *addr  load addr to a0   then load 0(a0) to a0
  case ND_DEREF:
    genExpr(Nd->LHS);
    load(Nd->Ty);
    return;
  // 取地址
  case ND_ADDR: // &a / & *a
    genAddr(Nd->LHS);
    return;
  // 语句表达式
  case ND_STMT_EXPR:
    for (Node *N = Nd->Body; N; N = N->Next)
      genStmt(N);
    return;
  // 逗号
  case ND_COMMA:
    genExpr(Nd->LHS);
    genExpr(Nd->RHS);
    return;
  // 类型转换
  case ND_CAST:
    genExpr(Nd->LHS);
    cast(Nd->LHS->Ty, Nd->Ty);
    return;
  // 内存清零
  case ND_MEMZERO: {
    printLn("  # 对%s的内存%d(fp)清零%d位", Nd->Var->Name, Nd->Var->Offset,
            Nd->Var->Ty->Size);
    // 对栈内变量所占用的每个字节都进行清零
    for (int I = 0; I < Nd->Var->Ty->Size; I++) {
      printLn("  li t0, %d", Nd->Var->Offset + I);
      printLn("  add t0, fp, t0");
      printLn("  sb zero, 0(t0)");
    }
    return;
  }
  // 条件运算符
  case ND_COND: {
    int C = count();
    printLn("\n# =====条件运算符%d===========", C);
    genExpr(Nd->Cond);
    notZero(Nd->Cond->Ty);
    printLn("  # 条件判断, 为0则跳转");
    printLn("  beqz a0, .L.else.%d", C);
    genExpr(Nd->Then);
    printLn("  # 跳转到条件运算符结尾部分");
    printLn("  j .L.end.%d", C);
    printLn(".L.else.%d:", C);
    genExpr(Nd->Els);
    printLn(".L.end.%d:", C);
    return;
  }
  // 函数调用
  case ND_FUNCALL: {
    // 计算所有参数的值，正向压栈
    pushArgs(Nd->Args);
    genExpr(Nd->LHS);
    // 将a0的值存入t0
    printLn("  mv t0, a0");

    // 反向弹栈，a0->参数1，a1->参数2……
    int GP = 0, FP = 0;
    // 读取函数形参中的参数类型
    Type *CurArg = Nd->FuncType->Params;
    for (Node *Arg = Nd->Args; Arg; Arg = Arg->Next) {
      // 如果是可变参数函数
      // 匹配到空参数（最后一个）的时候，将剩余的整型寄存器弹栈
      if (Nd->FuncType->IsVariadic && CurArg == NULL) {
        if (GP < 8) {
          printLn("  # a%d传递可变实参", GP);
          pop(GP++);
        }
        continue;
      }

      CurArg = CurArg->Next;
      if (isFloNum(Arg->Ty)) {
        if (FP < 8) {
          printLn("  # fa%d传递浮点参数", FP);
          popF(FP++);
        } else if (GP < 8) {
          printLn("  # a%d传递浮点参数", GP);
          pop(GP++);
        }
      } else {
        if (GP < 8) {
          printLn("  # a%d传递整型参数", GP);
          pop(GP++);
        }
      }
    }

    // 调用函数
    if (Depth % 2 == 0) {
      // 偶数深度，sp已经对齐16字节
      printLn("  # 调用函数");
      printLn("  jalr t0");
    } else {
      // 对齐sp到16字节的边界
      printLn("  # 对齐sp到16字节的边界, 并调用函数");
      printLn("  addi sp, sp, -8");
      printLn("  jalr t0");
      printLn("  addi sp, sp, 8");
    }

    // 清除寄存器中高位无关的数据
    switch (Nd->Ty->Kind) {
    case TY_BOOL:
      printLn("  # 清除bool类型的高位");
      printLn("  slli a0, a0, 63");
      printLn("  srli a0, a0, 63");
      return;
    case TY_CHAR:
      printLn("  # 清除char类型的高位");
      printLn("  slli a0, a0, 56");
      if (Nd->Ty->IsUnsigned)
        printLn("  srli a0, a0, 56");
      else
        printLn("  srai a0, a0, 56");
      return;
    case TY_SHORT:
      printLn("  # 清除short类型的高位");
      printLn("  slli a0, a0, 48");
      if (Nd->Ty->IsUnsigned)
        printLn("  srli a0, a0, 48");
      else
        printLn("  srai a0, a0, 48");
      return;
    default:
      break;
    }
    return;
  }
    return;
  default:
    break;
  }

  // 处理浮点类型
  if (isFloNum(Nd->LHS->Ty)) {
    // 递归到最右节点
    genExpr(Nd->RHS);
    // 将结果压入栈
    pushF();
    // 递归到左节点
    genExpr(Nd->LHS);
    // 将结果弹栈到fa1
    popF(1);

    // 生成各个二叉树节点
    // float对应s(single)后缀，double对应d(double)后缀
    char *Suffix = (Nd->LHS->Ty->Kind == TY_FLOAT) ? "s" : "d";

    switch (Nd->Kind) {
    case ND_ADD:
      printLn("  # fa0+fa1, 结果写入fa0");
      printLn("  fadd.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_SUB:
      printLn("  # fa0-fa1, 结果写入fa0");
      printLn("  fsub.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_MUL:
      printLn("  # fa0*fa1, 结果写入fa0");
      printLn("  fmul.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_DIV:
      printLn("  # fa0/fa1, 结果写入fa0");
      printLn("  fdiv.%s fa0, fa0, fa1", Suffix);
      return;
    case ND_EQ:
      printLn("  # 判断是否fa0=fa1");
      printLn("  feq.%s a0, fa0, fa1", Suffix);
      return;
    case ND_NE:
      printLn("  # 判断是否fa0≠fa1");
      printLn("  feq.%s a0, fa0, fa1", Suffix);
      printLn("  seqz a0, a0");
      return;
    case ND_LT:
      printLn("  # 判断是否fa0<fa1");
      printLn("  flt.%s a0, fa0, fa1", Suffix);
      return;
    case ND_LE:
      printLn("  # 判断是否fa0≤fa1");
      printLn("  fle.%s a0, fa0, fa1", Suffix);
      return;
    default:
      errorTok(Nd->Tok, "invalid expression");
    }
  }
  // 递归到最右节点
  genExpr(Nd->RHS);
  // 将结果压入栈
  push();
  // 递归到左节点
  genExpr(Nd->LHS);
  // 将结果弹栈到a1
  pop(1);

  // 生成各个二叉树节点
  char *Suffix = Nd->LHS->Ty->Kind == TY_LONG || Nd->LHS->Ty->Base ? "" : "w";
  switch (Nd->Kind) {
  case ND_ADD: // + a0=a0+a1
    printLn("  add%s a0, a0, a1", Suffix);
    return;
  case ND_SUB: // - a0=a0-a1
    printLn("  sub%s a0, a0, a1", Suffix);
    return;
  case ND_MUL: // * a0=a0*a1
    printLn("  mul%s a0, a0, a1", Suffix);
    return;
  case ND_DIV: // / a0=a0/a1
    if (Nd->Ty->IsUnsigned)
      printLn("  divu%s a0, a0, a1", Suffix);
    else
      printLn("  div%s a0, a0, a1", Suffix);
    return;
  case ND_MOD: // % a0=a0%a1
    if (Nd->Ty->IsUnsigned)
      printLn("  remu%s a0, a0, a1", Suffix);
    else
      printLn("  rem%s a0, a0, a1", Suffix);
    return;
  case ND_BITAND: // & a0=a0&a1
    printLn("  # a0&a1, 结果写入a0");
    printLn("  and a0, a0, a1");
    return;
  case ND_BITOR: // | a0=a0|a1
    printLn("  # a0|a1, 结果写入a0");
    printLn("  or a0, a0, a1");
    return;
  case ND_BITXOR: // ^ a0=a0^a1
    printLn("  # a0^a1, 结果写入a0");
    printLn("  xor a0, a0, a1");
    return;
  case ND_SHL:   // << a0=a0<<a1
    printLn(" # a0逻辑左移a1位");
    printLn("  sll%s a0, a0, a1", Suffix);
    return;
  case ND_SHR:   // >> a0=a0>>a1
    if (Nd->Ty->IsUnsigned)
      printLn("  srl%s a0, a0, a1", Suffix);
    else
      printLn("  sra%s a0, a0, a1", Suffix);
    return;
  case ND_EQ:
  case ND_NE:
    // a0=a0^a1，异或指令
    printLn("  xor a0, a0, a1");

    if (Nd->Kind == ND_EQ)
      // a0==a1
      // a0=a0^a1, sltiu a0, a0, 1
      // 等于0则置1
      printLn("  seqz a0, a0");
    else
      // a0!=a1
      // a0=a0^a1, sltu a0, x0, a0
      // 不等于0则置1
      printLn("  snez a0, a0");
    return;
  case ND_LT:
    if (Nd->LHS->Ty->IsUnsigned)
      printLn("  sltu a0, a0, a1");
    else
      printLn("  slt a0, a0, a1");
    return;
  case ND_LE:
    // a0<=a1等价于
    // a0=a1<a0, a0=a1^1
    if (Nd->LHS->Ty->IsUnsigned)
      printLn("  sltu a0, a1, a0");
    else
      printLn("  slt a0, a1, a0");
    printLn(" xori a0, a0, 1");
    return;
  default:
    errorTok(Nd->Tok, "invalid expression");
    break;
  }
}

// 生成语句
static void genStmt(Node *Nd) {
  // .loc 文件编号 行号
  printLn("  .loc 1 %d", Nd->Tok->LineNo);
  switch(Nd->Kind){
    // 生成for / while循环语句
    case ND_FOR: {
      // 代码段计数
      int C = count();
      // 生成初始化语句
      if (Nd->Init)
        genStmt(Nd->Init);
      // 输出循环头部标签
      printLn(".L.begin.%d:", C);
      // 处理循环条件语句
      if (Nd->Cond) {
        // 生成条件循环语句
        genExpr(Nd->Cond);
        notZero(Nd->Cond->Ty);
        // 判断结果是否为0，为0则跳转到结束部分
        printLn("  beqz a0, %s", Nd->BrkLabel);
      }
      // 生成循环体语句
      genStmt(Nd->Then);
      // continue标签语句
      printLn("%s:", Nd->ContLabel);
      // 处理循环递增语句
      if (Nd->Inc)
        // 生成循环递增语句
        genExpr(Nd->Inc);
      // 跳转到循环头部
      printLn("  j .L.begin.%d", C);
      // 输出循环尾部标签
      printLn("%s:", Nd->BrkLabel);
      return;
    }
    // 生成do while语句
    case ND_DO: {
      int C = count();
      printLn("\n# =====do while语句%d============", C);
      printLn("\n# begin语句%d", C);
      printLn(".L.begin.%d:", C);

      printLn("\n# Then语句%d", C);
      genStmt(Nd->Then);

      printLn("\n# Cond语句%d", C);
      printLn("%s:", Nd->ContLabel);
      genExpr(Nd->Cond);
      notZero(Nd->Cond->Ty);

      printLn("  # 跳转到循环%d的.L.begin.%d段", C, C);
      printLn("  bnez a0, .L.begin.%d", C);

      printLn("\n# 循环%d的%s段标签", C, Nd->BrkLabel);
      printLn("%s:", Nd->BrkLabel);
      return;
    }
    // 生成if语句
    case ND_IF: {
      // 代码段计数
      int C = count();
      // 生成条件内语句
      genExpr(Nd->Cond);
      notZero(Nd->Cond->Ty);
      // 判断结果是否为0，为0则跳转到else标签
      printLn("  beqz a0, .L.else.%d", C);
      // 生成符合条件后的语句
      genStmt(Nd->Then);
      // 执行完后跳转到if语句后面的语句
      printLn("  j .L.end.%d", C);
      // else代码块，else可能为空，故输出标签
      printLn(".L.else.%d:", C);
      // 生成不符合条件后的语句
      if (Nd->Els)
        genStmt(Nd->Els);
      // 结束if语句，继续执行后面的语句
      printLn(".L.end.%d:", C);
      return;
    }
    case ND_SWITCH:
      printLn("\n# =====switch语句===============");
      genExpr(Nd->Cond);

      printLn("  # 遍历跳转到值等于a0的case标签");
      for (Node *N = Nd->CaseNext; N; N = N->CaseNext) {
        printLn("  li t0, %ld", N->Val);
        printLn("  beq a0, t0, %s", N->Label);
      }

      if (Nd->DefaultCase) {
        printLn("  # 跳转到default标签");
        printLn("  j %s", Nd->DefaultCase->Label);
      }

      printLn("  # 结束switch, 跳转break标签");
      printLn("  j %s", Nd->BrkLabel);
      // 生成case标签的语句
      genStmt(Nd->Then);
      printLn("# switch的break标签, 结束switch");
      printLn("%s:", Nd->BrkLabel);
      return;
    case ND_CASE:
      printLn("# case标签, 值为%ld", Nd->Val);
      printLn("%s:", Nd->Label);
      genStmt(Nd->LHS);
      return;
    // 生成代码块，遍历代码块的语句链表
    case ND_BLOCK:
      for (Node *N = Nd->Body; N; N = N->Next)
        genStmt(N);
      return;
    // goto语句
    case ND_GOTO:
      printLn("  j %s", Nd->UniqueLabel);
      return;
    // 标签语句
    case ND_LABEL:
      printLn("%s:", Nd->UniqueLabel);
      genStmt(Nd->LHS);
      return;
    case ND_RETURN:
      // 不为空返回语句时
      if(Nd->LHS)
        genExpr(Nd->LHS);
      // 无条件跳转语句，跳转到.L.return段
      // j offset是 jal x0, offset的别名指令
      printLn("  # 跳转到.L.return.%s段", CurrentFn->Name);
      printLn("  j .L.return.%s", CurrentFn->Name);
      return;
    // 生成表达式语句
    case ND_EXPR_STMT:
      genExpr(Nd->LHS);
      return;
    default:
      break;
  }

  errorTok(Nd->Tok, "invalid statement");
}


// 根据变量的链表计算出偏移量
static void assignLVarOffsets(Obj *Prog) {
  // 为每个函数计算其变量所用的栈空间
  for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
    if(! Fn->IsFunction) // 不是函数
      continue;
    int Offset = 0;
    // 读取所有变量
    for (Obj *Var = Fn->Locals; Var; Var = Var->Next) {
      // 每个变量分配size字节
      Offset += Var->Ty->Size;
      // 对齐变量
      Offset = alignTo(Offset, Var->Align);
      // 为每个变量赋一个偏移量，或者说是栈中地址
      Var->Offset = -Offset;
    }
    // 将栈对齐到16字节
    Fn->StackSize = alignTo(Offset, 16);
  }
}

// 将整形寄存器的值存入栈中
static void storeGeneral(int Reg, int Offset, int Size) {
  printLn("  # 将a%d寄存器的值存入%d(fp)的栈地址",Reg, Offset);
  printLn("  li t0, %d", Offset);
  printLn("  add t0, fp, t0");
  switch (Size) {
  case 1:
    printLn("  sb a%d, 0(t0)", Reg);
    return;
  case 2:
    printLn("  sh a%d, 0(t0)", Reg);
    return;
  case 4:
    printLn("  sw a%d, 0(t0)", Reg);
    return;
  case 8:
    printLn("  sd a%d, 0(t0)", Reg);
    return;
  }
  unreachable();
}

// 将浮点寄存器的值存入栈中
static void storeFloat(int Reg, int Offset, int Sz) {
  printLn("  # 将fa%d寄存器的值存入%d(fp)的栈地址", Reg, Offset);
  printLn("  li t0, %d", Offset);
  printLn("  add t0, fp, t0");

  switch (Sz) {
  case 4:
    printLn("  fsw fa%d, 0(t0)", Reg);
    return;
  case 8:
    printLn("  fsd fa%d, 0(t0)", Reg);
    return;
  default:
    unreachable();
  }
}

void genFun(Obj *Fn){
  if (Fn->IsStatic) {
    printLn("\n  # 定义局部%s函数", Fn->Name);
    printLn("  .local %s", Fn->Name);
  } else {
    printLn("\n  # 定义全局%s函数", Fn->Name);
    printLn("  .globl %s", Fn->Name);
  }
  printLn("  .text");  // 后面要有 .data
  printLn("# =====%s段开始===============", Fn->Name);
  printLn("# %s段标签", Fn->Name);
  printLn("%s:", Fn->Name);
  CurrentFn = Fn;

  // 栈布局
  //-------------------------------// sp
  //              ra
  //-------------------------------// ra = sp-8
  //              fp
  //-------------------------------// fp = sp-16
  //             变量
  //-------------------------------// sp = sp-16-StackSize
  //           表达式计算
  //-------------------------------//

  // Prologue, 前言
  // 将ra寄存器压栈,保存ra的值
  printLn("  # 将ra寄存器压栈,保存ra的值");
  printLn("  addi sp, sp, -16"); // 分配两个位置
  printLn("  sd ra, 8(sp)");
  // 将fp压入栈中，保存fp的值
  printLn("  sd fp, 0(sp)");
  // 将sp写入fp
  printLn("  mv fp, sp");

  // 偏移量为实际变量所用的栈大小
  printLn("  li t0, -%d", Fn->StackSize);
  printLn("  add sp, sp, t0");

  // 记录整型寄存器，浮点寄存器使用的数量
  int GP = 0, FP = 0;
  for (Obj *Var = Fn->Params; Var; Var = Var->Next) {
    if (isFloNum(Var->Ty)) {
      printLn("  # 将浮点形参%s的浮点寄存器fa%d的值压栈", Var->Name, FP);
      storeFloat(FP++, Var->Offset, Var->Ty->Size);
    } else {
      printLn("  # 将整数形参%s的整型寄存器a%d的值压栈", Var->Name, GP);
      storeGeneral(GP++, Var->Offset, Var->Ty->Size);
    }
  }
  // 可变参数
  if (Fn->VaArea) {
    // 可变参数存入__va_area__，注意最多为7个
    int Offset = Fn->VaArea->Offset;
    while (GP < 8) {
      printLn("  # 可变参数，相对%s的偏移量为%d", Fn->VaArea->Name,
              Offset - Fn->VaArea->Offset);
      storeGeneral(GP++, Offset, 8);
      Offset += 8;
    }
  }
  
  // 生成语句链表的代码
  genStmt(Fn->Body);
  assert(Depth == 0);

  // Epilogue，后语
  // 输出return段标签
  printLn(".L.return.%s:", Fn->Name);
  // 将fp的值改写回sp
  printLn("  mv sp, fp");
  // 将最早fp保存的值弹栈，恢复fp。
  printLn("  ld fp, 0(sp)");
  // 将ra寄存器弹栈,恢复ra的值
  printLn("  # 将ra寄存器弹栈,恢复ra的值");
  printLn("  ld ra, 8(sp)");
  printLn("  addi sp, sp, 16");

  // 返回
  printLn("  ret");
}

// .data 全局变量
static void emitData(Obj *Prog) {
  for (Obj *Var = Prog; Var; Var = Var->Next) {
    // 跳过是函数或者无定义的变量
    if (Var->IsFunction || !Var->IsDefinition)
      continue;

    if (Var->IsStatic) {
      printLn("\n  # static全局变量%s", Var->Name);
      printLn("  .local %s", Var->Name);
    } else {
      printLn("\n  # 全局变量%s", Var->Name);
      printLn("  .globl %s", Var->Name);
    }
    printLn("  # 对齐全局变量");
    if (!Var->Ty->Align)
      error("Align can not be 0!");
    printLn("  .align %d", (int)log2(Var->Align));
    // 判断是否有初始值
    if (Var->InitData) {
      printLn("\n  # 数据段标签");
      printLn("  .data");
      printLn("%s:", Var->Name);
      // 打印出字符串的内容，包括转义字符
      Relocation *Rel = Var->Rel;
      int Pos = 0;
      while (Pos < Var->Ty->Size) {
        if (Rel && Rel->Offset == Pos) {
          // 使用其他变量进行初始化
          printLn("  # %s全局变量", Var->Name);
          printLn("  .quad %s%+ld", Rel->Label, Rel->Addend);
          Rel = Rel->Next;
          Pos += 8;
        } else {
          // 打印出字符串的内容，包括转义字符
          printLn("  # 字符串字面量");
          char C = Var->InitData[Pos++];
          if (isprint(C))
            printLn("  .byte %d\t# %c", C, C);
          else
            printLn("  .byte %d", C);
        }
      }
    } else {
      // bss段未给数据分配空间，只记录数据所需空间的大小
      printLn("  # 未初始化的全局变量");
      printLn("  .bss");
      printLn("%s:", Var->Name);
      printLn("  # 全局变量零填充%d位", Var->Ty->Size);
      printLn("  .zero %d", Var->Ty->Size);
    }
  }
}

// .test 函数
void emitText(Obj *Prog) {
  // 为每个函数单独生成代码
  for (Obj *Fn = Prog; Fn; Fn = Fn->Next) {
    if(!Fn->IsFunction || !Fn->IsDefinition)
      continue;
    genFun(Fn);
  }
}

// 代码生成入口函数，包含代码块的基础信息
void codegen(Obj *Prog, FILE *out){
  // 设置目标文件的文件流指针
  OutputFile = out;
  // 计算局部变量的偏移量
  assignLVarOffsets(Prog);
  // 生成数据
  emitData(Prog);
  // 生成代码
  emitText(Prog);
}