# DIGITAL MOSH: Palettes from the paintings

The owner asked for colour taken from actual Surrealist paintings, not invented "dreamy pastels". Every colour in
the scene's stages comes from the extraction below, or from a single named operation on an extracted colour.

## Method

1. **Reproductions.**
   - WikiArt for *The Persistence of Memory*, Tanguy's four works and Ernst. These are the largest.
   - Wikipedia for the rest: their fair-use files, 270-500 px. Small, but enough for colour clusters, which are
     averages over thousands of pixels.
   - Not available from either source: Magritte's *The Listening Room*, Tanguy's *The Furniture of Time*, Ernst's
     *The Eye of Silence*. They are not used, and no values were guessed for them.
2. **Extraction.** `examples/digital-mosh/palette_extract.py` thumbnails each image to 360 px and runs k-means
   (k-means++ initialisation) in CIELAB:
   - k = 8 over the whole picture;
   - k = 3 over the top 35% (the sky band);
   - k = 4 over the bottom 40% (the land band).

   The tables at the end give each cluster's share, its mean sRGB colour, its linear value, L* and chroma.
3. **Caveat.** The values are display-referred colours of photographs of varnished oil paint. They are used as
   albedo and sky radiance after sRGB-to-linear conversion, and exposure is set by eye on the result, not by the
   numbers.
4. **Review.** The swatch sheet (each painting beside its clusters) is in
   `~/Desktop/av-gen-review/38-digital-mosh/palettes/` (not in the repo: it contains reproductions).

## The mapping: one painting per stage, by role

*Pass 4: the owner's note that stage changes read as whole-frame tints. Infection and Corruption no longer swap the
frame's palette. Ernst's colours arrive only where the contagion is. Tanguy's grey-blue (`#b8c9c6`, `#9ab3bc`,
`#79898d`) was Infection's land through pass 3; it is not used now. The extraction below is unchanged.*

| Stage | Source | Sky (zenith / horizon) | Land (flat / slope / cliff) | Light | Haze | Accent |
|---|---|---|---|---|---|---|
| **Dream** | Dalí, *Dream Caused by the Flight of a Bee*; Dalí, *The Persistence of Memory* | `#94acbe` / `#e4d7bf` (Bee) | `#e4d7bf` salt pan (Bee); `#b1b081` olive-gold, `#8c6f36` ochre (Persistence); `#574625` cliff (Persistence) | warm cream `#e4d7bf` (Bee) | `#e4d7bf` (Bee's horizon; pass 3: its sky blue `#b1c9d8` washed the land grey) | Cap de Creus teal `#3f798d` in the shadows' sky light (Persistence) |
| **Uncanny** | de Chirico, *The Disquieting Muses*; *Mystery and Melancholy of a Street* | `#3b6e65` green-teal / `#efe1ab` (Muses) | unchanged: the land is still the Dream's | Chirico's late orange `#e49420` (Muses) | `#f6e6be` cream (Street) | green-black shadow `#223b37` (Street) |
| **Infection** | de Chirico's sky continues (pass 4); the Dream's land | as the Uncanny | the Dream's, **except where the contagion is** | as the Uncanny | as the Uncanny | the strain (below) |
| **Corruption** | as Infection; Ernst, *Europe After the Rain II*, arrives with the stain | as the Uncanny | the Dream's, turned to Ernst's rust `#975736` a few metres ahead of the ink `#2d0c1f` | as the Uncanny | as the Uncanny | the strain at full chroma |
| **Nightmare** | Dalí, *The Elephants* (sky); Tanguy, *Slowly Toward the North* (land) | blood red `#8f2b30` / `#b35a43` (Elephants) | `#36444e`, `#1d2c35` (North) | `#c27f54` (Elephants) | `#563529` (Elephants) | |
| **Collapse** | Tanguy, *Multiplication of the Arcs* | `#373943` / `#aaaeb6` | the debris greys `#555962`, `#747a84`, `#9098a3` | | `#9098a3` | it burns out to `#ebeeec`, its own white |
| **Respite** | Magritte, *The Empire of Light* | day `#94c0d9` / `#e1e4d9` | **night**: `#25292a`, `#323637` | | `#505655` | a temporary recovery as Magritte's paradox: a day sky over a land still in night |
| **Recovery** | the Dream, exactly (P8) | | | | | the stuck macroblock |

### The corruption colours are a painting being infected

- **The ink.** A failed cell is Ernst's aubergine `#2d0c1f` (linear 0.026, 0.004, 0.014). This is the darkest
  cluster of a painting of a continent rotting.
- **Strain one: impossible red.** Ernst's oxblood `#4f1a23` (OKLCH hue 12.9°) is taken to the maximum chroma sRGB
  allows at L 0.62, which gives linear (0.913, 0.003, 0.104). Its hue is the painting's. Its chroma is beyond what
  paint can reach, and that is the infection.
- **Strain two: toxic chartreuse.** The pale green `#b1cb9e` of Dalí's *The Temptation of Saint Anthony* (hue
  132.2°) taken to maximum chroma at L 0.88 gives linear (0.305, 0.926, 0.001). When the music is bright, timbre
  rotates the strain from one to the other: a +0.331-turn hue shift in OKLCH, routed.
- **The fracture light** is strain one, so the same crimson reaches the light, the haze and the stone's specular.

## Extracted values

### Salvador Dalí, *The Persistence of Memory* (1931), MoMA

Source: WikiArt reproduction. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 32% | `#201f1f` | [0.0144, 0.014, 0.014] | 12.0 | 0.3 |
| 17% | `#574625` | [0.0959, 0.0604, 0.0187] | 30.6 | 22.4 |
| 12% | `#3c3427` | [0.0452, 0.034, 0.0204] | 22.1 | 9.5 |
| 10% | `#3f798d` | [0.05, 0.1913, 0.2643] | 47.9 | 20.5 |
| 10% | `#626258` | [0.1226, 0.1218, 0.0981] | 41.2 | 5.8 |
| 8% | `#87a79e` | [0.2425, 0.3843, 0.3443] | 65.9 | 12.3 |
| 8% | `#b1b081` | [0.4405, 0.4367, 0.2193] | 71.0 | 25.6 |
| 4% | `#8c6f36` | [0.262, 0.1597, 0.0373] | 48.6 | 35.7 |

Top band (sky) `#a6ac85`, `#4d8290`, `#5c533a`; bottom band (land) `#1f1f1f`, `#684c25`, `#463a2d`, `#6a6e66`.

### Salvador Dalí, *The Elephants* (1948)

Source: Wikipedia (fair-use reproduction, 362 px). Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 32% | `#a03334` | [0.3512, 0.0329, 0.0342] | 38.0 | 51.4 |
| 18% | `#8f2b30` | [0.2748, 0.0246, 0.0298] | 33.7 | 47.0 |
| 15% | `#b35a43` | [0.4502, 0.1014, 0.0561] | 48.6 | 45.3 |
| 12% | `#c27f54` | [0.5391, 0.2132, 0.0876] | 59.3 | 40.3 |
| 10% | `#cb9d62` | [0.5964, 0.3378, 0.1214] | 67.8 | 38.7 |
| 5% | `#7d6047` | [0.2038, 0.1163, 0.0638] | 42.9 | 20.2 |
| 5% | `#a6896c` | [0.3816, 0.249, 0.1501] | 59.0 | 20.8 |
| 3% | `#563529` | [0.0924, 0.0351, 0.0224] | 25.7 | 18.9 |

Top band (sky) `#952d32`, `#693c34`, `#a68375`; bottom band (land) `#c8935d`, `#b86547`, `#947a58`, `#624934`.

### Salvador Dalí, *Dream Caused by the Flight of a Bee Around a Pomegranate a Second Before Awakening* (1944), Thyssen-Bornemisza

Source: Wikipedia (281 px). Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 34% | `#b1c9d8` | [0.4393, 0.5812, 0.6869] | 79.5 | 11.4 |
| 17% | `#e4d7bf` | [0.7792, 0.6809, 0.5238] | 86.5 | 13.4 |
| 13% | `#94acbe` | [0.2973, 0.4138, 0.5126] | 69.2 | 12.4 |
| 13% | `#b6b8af` | [0.4698, 0.4775, 0.4309] | 74.3 | 4.5 |
| 8% | `#9f9985` | [0.3485, 0.3193, 0.2329] | 63.3 | 11.7 |
| 6% | `#666e6c` | [0.1333, 0.1573, 0.1513] | 45.8 | 3.4 |
| 5% | `#7e6030` | [0.2092, 0.1168, 0.0296] | 42.7 | 32.2 |
| 4% | `#c1ae5e` | [0.5328, 0.4251, 0.1117] | 71.2 | 43.3 |

Top band (sky) `#adc6d7`, `#9bafbe`, `#8c7e48`; bottom band (land) `#c6bfae`, `#9bb3c4`, `#7a807c`, `#73512a`.

### Salvador Dalí, *The Temptation of Saint Anthony* (1946), Royal Museums of Fine Arts of Belgium

Source: Wikipedia (300 px). Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 16% | `#444b38` | [0.057, 0.0703, 0.0397] | 30.8 | 12.1 |
| 16% | `#76a490` | [0.1802, 0.3728, 0.2782] | 63.7 | 20.6 |
| 15% | `#717553` | [0.1643, 0.1777, 0.0874] | 48.0 | 19.5 |
| 14% | `#9b9e6f` | [0.3277, 0.3436, 0.1584] | 63.9 | 26.1 |
| 14% | `#d1cd82` | [0.6389, 0.6097, 0.224] | 81.2 | 38.8 |
| 12% | `#b1cb9e` | [0.4379, 0.595, 0.3401] | 78.7 | 25.8 |
| 9% | `#1a2220` | [0.0101, 0.0158, 0.0145] | 12.6 | 2.7 |
| 3% | `#93782a` | [0.2939, 0.1871, 0.0231] | 51.4 | 44.5 |

Top band (sky) `#7a967c`, `#233430`, `#b1a55e`; bottom band (land) `#40402e`, `#82845c`, `#b9c38c`, `#dbd788`.

### Yves Tanguy, *Indefinite Divisibility* (1942), Albright-Knox

Source: WikiArt. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 23% | `#b8c9c6` | [0.4772, 0.585, 0.5665] | 79.7 | 6.5 |
| 20% | `#9ab3bc` | [0.3209, 0.4497, 0.5045] | 71.3 | 10.1 |
| 17% | `#d7e2da` | [0.6761, 0.7609, 0.6981] | 88.8 | 6.1 |
| 11% | `#84add1` | [0.2326, 0.4171, 0.6406] | 69.0 | 23.3 |
| 10% | `#79898d` | [0.1914, 0.2505, 0.2674] | 56.0 | 6.2 |
| 10% | `#162021` | [0.0081, 0.0141, 0.015] | 11.2 | 4.0 |
| 7% | `#505655` | [0.081, 0.0931, 0.0917] | 36.1 | 2.0 |
| 1% | `#cd946c` | [0.611, 0.2963, 0.151] | 65.9 | 33.9 |

Top band (sky) `#c7d6cd`, `#a0b8b7`, `#76929a`; bottom band (land) `#8ea2ac`, `#1a2223`, `#616664`, `#d5d9d1`.

### Yves Tanguy, *Mama, Papa is Wounded!* (1927), MoMA

Source: WikiArt. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 24% | `#a0aad5` | [0.3511, 0.4033, 0.6632] | 70.2 | 23.4 |
| 19% | `#b5caee` | [0.4631, 0.5878, 0.8537] | 80.8 | 20.2 |
| 16% | `#7e77a6` | [0.208, 0.1844, 0.3819] | 52.2 | 27.6 |
| 14% | `#523f3d` | [0.0849, 0.0499, 0.0462] | 28.6 | 9.4 |
| 10% | `#d3f0f9` | [0.6483, 0.8728, 0.9444] | 93.0 | 10.7 |
| 8% | `#392439` | [0.0407, 0.0178, 0.0407] | 17.6 | 16.8 |
| 7% | `#584672` | [0.0974, 0.0616, 0.1668] | 33.3 | 28.1 |
| 3% | `#988054` | [0.3143, 0.2163, 0.0892] | 54.8 | 27.1 |

Top band (sky) `#c1dff6`, `#493540`, `#7771aa`; bottom band (land) `#a0abd5`, `#becdea`, `#453340`, `#917758`.

### Yves Tanguy, *Slowly Toward the North* (1942), MoMA

Source: WikiArt. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 30% | `#36444e` | [0.0365, 0.0575, 0.0762] | 28.0 | 8.4 |
| 23% | `#1d2c35` | [0.0124, 0.0254, 0.0352] | 17.1 | 8.2 |
| 15% | `#4f697b` | [0.0773, 0.1421, 0.1969] | 43.1 | 13.7 |
| 12% | `#565e5e` | [0.0931, 0.1126, 0.1117] | 39.3 | 3.2 |
| 11% | `#5c88ab` | [0.1068, 0.2451, 0.4048] | 54.8 | 23.6 |
| 4% | `#e0e7e9` | [0.7473, 0.7991, 0.8117] | 91.2 | 2.4 |
| 4% | `#99a6aa` | [0.3179, 0.3825, 0.4013] | 67.3 | 4.8 |
| 2% | `#c7774c` | [0.5716, 0.1859, 0.0729] | 58.2 | 45.5 |

Top band (sky) `#2f3f4b`, `#4a5f6f`, `#e2e4e8`; bottom band (land) `#223036`, `#5a656a`, `#bfc9ca`, `#c4754b`.

### Yves Tanguy, *Multiplication of the Arcs* (1954), MoMA

Source: WikiArt. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 20% | `#aaaeb6` | [0.3994, 0.4245, 0.4669] | 71.0 | 4.5 |
| 17% | `#9098a3` | [0.2796, 0.3145, 0.3681] | 62.6 | 6.9 |
| 12% | `#747a84` | [0.1747, 0.1951, 0.229] | 51.1 | 5.8 |
| 12% | `#555962` | [0.0917, 0.0997, 0.1217] | 37.8 | 5.6 |
| 12% | `#c7c9cf` | [0.5704, 0.5858, 0.621] | 81.0 | 2.9 |
| 11% | `#373943` | [0.0377, 0.0416, 0.0562] | 24.3 | 6.4 |
| 11% | `#181c26` | [0.0093, 0.0115, 0.0192] | 10.4 | 7.2 |
| 6% | `#ebeeec` | [0.8313, 0.8516, 0.8348] | 93.7 | 1.4 |

Top band (sky) `#a9aeb7`, `#8a94a3`, `#cecfd5`; bottom band (land) `#51545b`, `#1e222b`, `#898e92`, `#d1d7d6`.

### René Magritte, *The Empire of Light* (1953-54 version)

Source: Wikipedia (279 px). Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 27% | `#25292a` | [0.0185, 0.0224, 0.0234] | 16.3 | 2.1 |
| 19% | `#94c0d9` | [0.298, 0.5293, 0.6903] | 75.6 | 18.9 |
| 15% | `#1c2021` | [0.0117, 0.0146, 0.0154] | 12.0 | 2.1 |
| 12% | `#e1e4d9` | [0.7535, 0.7767, 0.6933] | 90.1 | 5.9 |
| 12% | `#b6ccd2` | [0.469, 0.6038, 0.6415] | 80.6 | 8.1 |
| 10% | `#323637` | [0.0317, 0.0371, 0.0384] | 22.3 | 2.0 |
| 4% | `#505655` | [0.08, 0.0931, 0.0911] | 36.0 | 2.5 |
| 2% | `#82877c` | [0.2242, 0.2437, 0.2002] | 55.7 | 7.0 |

Top band (sky) `#97c1d8`, `#d9e1da`, `#343a3c`; bottom band (land) `#2a2e2f`, `#1e2323`, `#4c514f`, `#838570`.

### René Magritte, *The Son of Man* (1964)

Source: Wikipedia (276 px). Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 22% | `#4b5765` | [0.0703, 0.0944, 0.1296] | 36.3 | 9.7 |
| 22% | `#a5a6aa` | [0.3742, 0.3829, 0.4023] | 68.2 | 2.1 |
| 19% | `#9198a1` | [0.2853, 0.3142, 0.3587] | 62.6 | 5.8 |
| 13% | `#bababc` | [0.4922, 0.4922, 0.5045] | 75.6 | 1.1 |
| 10% | `#8f8780` | [0.2749, 0.2438, 0.2147] | 56.9 | 5.4 |
| 10% | `#2e3948` | [0.0268, 0.0407, 0.0642] | 23.4 | 10.6 |
| 2% | `#d8c4b4` | [0.6842, 0.5534, 0.4569] | 80.4 | 11.5 |
| 2% | `#84574c` | [0.2304, 0.095, 0.0719] | 41.6 | 22.3 |

Top band (sky) `#969a9d`, `#b1b1b2`, `#464747`; bottom band (land) `#4c5765`, `#9b8d88`, `#959dab`, `#2e3949`.

### Giorgio de Chirico, *Mystery and Melancholy of a Street* (1914)

Source: Wikipedia. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 41% | `#101410` | [0.0052, 0.0071, 0.0051] | 6.3 | 3.2 |
| 20% | `#242919` | [0.0173, 0.0223, 0.01] | 15.7 | 11.1 |
| 12% | `#d18e2b` | [0.6366, 0.2713, 0.0245] | 64.2 | 61.3 |
| 9% | `#f6e6be` | [0.9233, 0.7886, 0.5122] | 91.5 | 21.7 |
| 8% | `#223b37` | [0.0163, 0.0438, 0.0385] | 22.8 | 10.5 |
| 4% | `#4d6b52` | [0.0736, 0.1478, 0.0837] | 42.4 | 19.7 |
| 3% | `#947531` | [0.2947, 0.1764, 0.0303] | 50.8 | 41.0 |
| 3% | `#4f4621` | [0.0774, 0.0611, 0.0156] | 29.8 | 23.2 |

Top band (sky) `#1b241f`, `#f5e4bc`, `#405c47`; bottom band (land) `#10130c`, `#222616`, `#c98827`, `#7f6124`.

### Giorgio de Chirico, *The Disquieting Muses* (1916-18)

Source: Wikipedia (500 px). Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 21% | `#3b6e65` | [0.0437, 0.1561, 0.1311] | 42.8 | 19.2 |
| 19% | `#e49420` | [0.7746, 0.2949, 0.0142] | 67.8 | 70.1 |
| 17% | `#58473c` | [0.0985, 0.0635, 0.0451] | 31.7 | 11.1 |
| 16% | `#70533a` | [0.1623, 0.0876, 0.0417] | 37.9 | 21.6 |
| 10% | `#d17726` | [0.6404, 0.1848, 0.0193] | 59.0 | 63.4 |
| 8% | `#a0753c` | [0.3525, 0.1787, 0.0458] | 52.6 | 38.9 |
| 7% | `#efe1ab` | [0.8611, 0.7532, 0.4079] | 89.4 | 28.4 |
| 2% | `#eaca52` | [0.8223, 0.5918, 0.084] | 82.1 | 60.3 |

Top band (sky) `#3a6b63`, `#685240`, `#e49738`; bottom band (land) `#d78523`, `#674d36`, `#e9d99c`, `#438c81`.

### Max Ernst, *Europe After the Rain II* (1940-42), Wadsworth Atheneum

Source: WikiArt. Clusters (k-means in CIELAB, k = 8, by share): 

| share | sRGB | linear | L* | chroma |
|---|---|---|---|---|
| 29% | `#c7d2ce` | [0.5739, 0.6476, 0.6156] | 83.4 | 4.5 |
| 17% | `#2d0c1f` | [0.0267, 0.0038, 0.0139] | 8.8 | 19.1 |
| 14% | `#4f1a23` | [0.0784, 0.0104, 0.0168] | 18.2 | 26.4 |
| 12% | `#975736` | [0.3113, 0.0944, 0.0373] | 43.8 | 38.3 |
| 12% | `#732e26` | [0.1722, 0.0277, 0.0195] | 29.1 | 35.9 |
| 7% | `#5f4c46` | [0.114, 0.0713, 0.0604] | 34.0 | 10.1 |
| 5% | `#b47222` | [0.4557, 0.168, 0.0162] | 53.9 | 54.6 |
| 5% | `#a58971` | [0.3786, 0.2486, 0.1643] | 59.0 | 18.7 |

Top band (sky) `#bfcdca`, `#985835`, `#421c24`; bottom band (land) `#371023`, `#6b3431`, `#9d623b`, `#d5d0c1`.
