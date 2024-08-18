package main

import (
	"github.com/stretchr/testify/assert"
	"image/png"
	"os"
	"testing"
)

func TestGetPixel(t *testing.T) {
	file, _ := os.Open("z12_red_green.png")
	defer file.Close()
	img, _ := png.Decode(file)
	assert.Equal(t, 249.125, GetPixel(img, 14, 2620, 6331))
}
