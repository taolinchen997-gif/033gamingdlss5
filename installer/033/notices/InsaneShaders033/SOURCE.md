# 033 pre-NR anime colour steps

Upstream: https://github.com/LordOfLunacy/Insane-Shaders/blob/19397d503e2fbf1ad2cbedb35fbf2ee84a32e3ec/Shaders/BilateralComic.fx
Commit: 19397d503e2fbf1ad2cbedb35fbf2ee84a32e3ec
Author: Lord of Lunacy. License: CC0-1.0; original source and LICENSE included.

033 adapts the luminance/chroma quantization concept in OutputPS. Its implementation uses soft continuous steps in log luminance, bounded luminance scaling for HDR, optional input skin protection, and no neighbours or previous-frame samples. This is a partial adaptation, not the full BilateralComic shader: bilateral smoothing, depth normals and outlines are not included. Natural and cinema are independently authored numeric colour recipes. These shaders do not implement AnimeGAN, photo-to-anime facial redraw, or a new neural network.
