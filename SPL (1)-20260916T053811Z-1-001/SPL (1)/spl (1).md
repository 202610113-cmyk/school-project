# SPL — Sparse Pragmatic Language v0.1

> 최소한의 실험 언어. 모든 키워드는 **4바이트**.

## 1. 기본 규칙

- 한 줄에 하나의 명령문
- 키워드는 항상 줄의 첫 4글자
- `;` 이후는 주석
- 빈 줄은 무시

## 2. 값 타입

| 타입 | 설명 | 예시 |
|------|------|------|
| 정수 | 64비트 정수 | `42`, `-7` |
| 실수 | 64비트 부동소수점 | `3.14` |
| 문자열 | 큰따옴표로 감싸기 | `"hello"` |

## 3. 키워드

### 3.1 변수

| 키워드 | 의미 | 문법 |
|--------|------|------|
| `varm` | 변수 선언 (변경 가능) | `varm name value` |
| `varl` | 상수 선언 (변경 불가) | `varl name value` |

```
varl pi   3.14
varm cnt  0
varm msg  "hello"
```

### 3.2 대입

| 키워드 | 의미 | 문법 |
|--------|------|------|
| `set_` | 변수에 값 대입 | `set_ name value` |

```
set_ cnt  10
set_ msg  "world"
```

### 3.3 산술 (결과를 변수에 저장)

| 키워드 | 의미 | 문법 |
|--------|------|------|
| `add_` | 덧셈 | `add_ dest a b` |
| `sub_` | 뺄셈 | `sub_ dest a b` |
| `mul_` | 곱셈 | `mul_ dest a b` |
| `div_` | 나눗셈 | `div_ dest a b` |
| `mod_` | 나머지 | `mod_ dest a b` |

- `a`, `b`는 리터럴 또는 변수 이름
- 결과는 `dest` 변수에 저장 (없으면 자동 생성)

```
add_ x 10 20     ; x = 30
mul_ y x 2       ; y = 60
sub_ z y 10      ; z = 50
```

### 3.4 출력

| 키워드 | 의미 | 문법 |
|--------|------|------|
| `prnt` | 줄바꿈 포함 출력 | `prnt value` |

- `value`는 리터럴, 변수이름, 또는 `"문자열"`

```
prnt "hello world"
prnt cnt
prnt 42
```

### 3.5 입력

| 키워드 | 의미 | 문법 |
|--------|------|------|
| `inpt` | stdin에서 한 줄 읽기 | `inpt varname` |

```
inpt name     ; 사용자 입력을 name에 문자열로 저장
```

### 3.6 조건문

| 키워드 | 의미 |
|--------|------|
| `if__` | 조건 시작 |
| `elif` | else-if |
| `else` | else |
| `end_` | 블록 종료 |

비교 연산자: `==`, `!=`, `<`, `>`, `<=`, `>=`

```
if__ cnt > 5
  prnt "big"
elif cnt == 5
  prnt "five"
else
  prnt "small"
end_
```

### 3.7 반복문

| 키워드 | 의미 |
|--------|------|
| `loop` | while 루프 |
| `brk_` | break |
| `end_` | 블록 종료 |

```
varm i 0
loop i < 5
  prnt i
  add_ i i 1
end_
```

### 3.8 함수

| 키워드 | 의미 |
|--------|------|
| `func` | 함수 정의 |
| `retn` | 값 반환 |
| `call` | 함수 호출 |
| `end_` | 블록 종료 |

```
func add(a b)
  add_ r a b
  retn r
end_

call add(3 4)          ; 호출만 (결과 버림)
varm result add(3 4)   ; 결과를 변수에 저장
prnt result
```

### 3.9 레이블 / 점프

| 키워드 | 의미 |
|--------|------|
| `labl` | 레이블 정의 |
| `goto` | 레이블로 점프 |

```
labl start
prnt "loop"
goto start
```

## 4. 전체 예제

```
; 1부터 10까지 합산
varm sum 0
varm i   1
loop i <= 10
  add_ sum sum i
  add_ i   i   1
end_
prnt "sum ="
prnt sum
```
