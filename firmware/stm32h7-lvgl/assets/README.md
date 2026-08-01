# assets — 보드에 올릴 파일들

`upload.py --sync` 가 이 폴더 구조를 그대로 보드에 복사한다.

```
assets/sd/**   -> SD 카드
assets/spi/**  -> SPI Flash (littlefs)
```

```sh
# 이 폴더 전체를 보드로
./tools/python/upload.py -p /dev/cu.usbmodemXXXX --sync assets
```

**SD 카드를 PC 에 직접 꽂아 복사해도 된다.** 용량이 크면 그쪽이 훨씬 빠르다
(USB CDC 는 약 220 KB/s).

---

## 무엇이 저장소에 들어 있고 무엇이 아닌가

작은 생성물은 커밋해 두고, 큰 것과 라이선스가 남의 것인 파일은 **폴더와 설명만**
둔다. 받는 사람이 직접 채운다.

| | 저장소 | 이유 |
|---|---|---|
| `sd/prog/mcu/st.txt` | **들어 있다** (18 KB) | 생성에 CubeProgrammer 설치가 필요한데 모두가 깔았을 리 없다 |
| `sd/prog/loaders/**` | 폴더만 | 30 MB 고 ST/ARM 배포물이다 |
| `sd/prog/fw/**` | 폴더만 | 각자의 펌웨어 |
| `spi/font/*.bin` | **들어 있다** (890 KB) | 만들려면 node 나 TTF 가 필요하다 |

각 폴더의 `README.md` 에 무엇을 어디서 가져와 넣는지 적어뒀다.
