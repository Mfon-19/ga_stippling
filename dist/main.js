"use strict";
// dots on a white canvas
// dots are randomly displaced
// how many dots? variable
// dot size? variable
// Canvas for our genetic algo
const canvas = document.getElementById("canvas");
// Canvas for the target image
const imgCanvas = document.getElementById("imgCanvas");
// Canvas for the target image in black and white
const bwCanvas = document.getElementById("bwCanvas");
// Canvas for the target image in black and white
const evolCanvas = document.getElementById("evolCanvas");
// Get the context for our genetic algo
const ctx = canvas.getContext("2d", { willReadFrequently: true });
// Context for our target image
const imgCtx = imgCanvas.getContext("2d");
// Context for our target image in black and white
const bwCtx = bwCanvas.getContext("2d");
// Context for our target image in black and white
const evolCtx = evolCanvas.getContext("2d");
const dotCountElement = document.getElementById("recommendedDotCount");
if (!imgCtx || !bwCtx || !ctx || !evolCtx)
    throw new Error("Failed to get canvas contexts");
ctx.fillStyle = "black";
const endAngle = 2 * Math.PI;
let generations = 0;
let recommendedDotCount = 0;
let blurAmount = 0;
let threshold = 130;
// Add event listeners for the sliders
const blurSlider = document.getElementById("blurAmount");
const thresholdSlider = document.getElementById("threshold");
const blurValueDisplay = document.getElementById("blurValue");
const thresholdValueDisplay = document.getElementById("thresholdValue");
if (blurSlider && thresholdSlider) {
    blurSlider.addEventListener("input", (e) => {
        blurAmount = parseInt(e.target.value);
        blurValueDisplay.textContent = blurAmount.toString();
        if (img) {
            convertToBlackAndWhite();
            calculateRecommendedDotCount();
        }
    });
    thresholdSlider.addEventListener("input", (e) => {
        threshold = parseInt(e.target.value);
        thresholdValueDisplay.textContent = threshold.toString();
        if (img) {
            convertToBlackAndWhite();
            calculateRecommendedDotCount();
        }
    });
}
const startEvolution = () => {
    if (!img)
        return;
    const algo = new GeneticAlgorithm(300, 0.3, bwCtx.getImageData(0, 0, img === null || img === void 0 ? void 0 : img.width, img === null || img === void 0 ? void 0 : img.height), ctx, recommendedDotCount);
    const intervalId = setInterval(() => {
        algo.evolve();
        generations++;
        if (dotCountElement) {
            dotCountElement.textContent = `Recommended dot count: ${recommendedDotCount}. Generations: ${generations}`;
        }
        const fittestIndex = algo.population.fittestIndividual();
        algo.population.drawIndividual(algo.population.population[fittestIndex].dots, evolCtx);
        console.log("Generations: ", generations);
        console.log("Fittest individual fitness: ", algo.population.population[fittestIndex].fitness);
        console.log(`Fitness scores: ${algo.population.population
            .map((ind) => ind.fitness)
            .join(", ")}`);
    }, 1000 / 60);
};
const prepareTarget = () => {
    var _a;
    const input = document.getElementById("imgInput");
    const file = (_a = input === null || input === void 0 ? void 0 : input.files) === null || _a === void 0 ? void 0 : _a[0];
    if (!file)
        return;
    const reader = new FileReader();
    reader.onload = function (e) {
        var _a;
        img = new Image();
        img.onload = function () {
            // Set canvas dimensions to match the image
            if (!img)
                return;
            imgCanvas.width = img.width;
            imgCanvas.height = img.height;
            bwCanvas.width = img.width;
            bwCanvas.height = img.height;
            canvas.width = img.width;
            canvas.height = img.height;
            evolCanvas.width = img.width;
            evolCanvas.height = img.height;
            // Draw the original image
            imgCtx.drawImage(img, 0, 0);
            convertToBlackAndWhite();
            calculateRecommendedDotCount();
        };
        img.src = (_a = e === null || e === void 0 ? void 0 : e.target) === null || _a === void 0 ? void 0 : _a.result;
    };
    reader.readAsDataURL(file);
};
// Add event listener to the file input
const fileInput = document.getElementById("imgInput");
if (fileInput) {
    fileInput.addEventListener("change", prepareTarget);
}
const startBtn = document.getElementById("start");
if (startBtn) {
    startBtn.addEventListener("click", startEvolution);
}
let img = null;
const convertToBlackAndWhite = () => {
    if (!img)
        return;
    // Get image data from the original image canvas
    const imageData = imgCtx.getImageData(0, 0, imgCanvas.width, imgCanvas.height);
    const data = imageData.data;
    // Convert each pixel to grayscale
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i + 1];
        const b = data[i + 2];
        // Use the standard luminance formula for grayscale conversion
        const gray = 0.299 * r + 0.587 * g + 0.114 * b;
        data[i] = gray; // Red
        data[i + 1] = gray; // Green
        data[i + 2] = gray; // Blue
        // Alpha channel (data[i + 3]) remains unchanged
    }
    const width = imgCanvas.width;
    const height = imgCanvas.height;
    const tempData = new Uint8ClampedArray(data.length);
    // Simple box blur algorithm
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            let r = 0, g = 0, b = 0, count = 0;
            // Sample neighboring pixels
            for (let dy = -blurAmount; dy <= blurAmount; dy++) {
                for (let dx = -blurAmount; dx <= blurAmount; dx++) {
                    const nx = x + dx;
                    const ny = y + dy;
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        const i = (ny * width + nx) * 4;
                        r += data[i];
                        g += data[i + 1];
                        b += data[i + 2];
                        count++;
                    }
                }
            }
            const i = (y * width + x) * 4;
            tempData[i] = r / count;
            tempData[i + 1] = g / count;
            tempData[i + 2] = b / count;
            tempData[i + 3] = data[i + 3]; // Keep original alpha
        }
    }
    // Copy back the blurred result
    for (let i = 0; i < data.length; i++) {
        data[i] = tempData[i];
    }
    // Apply threshold to create pure black and white
    for (let i = 0; i < data.length; i += 4) {
        const value = data[i] < threshold ? 0 : 255;
        data[i] = value; // Red
        data[i + 1] = value; // Green
        data[i + 2] = value; // Blue
    }
    // Draw the black and white image on the second canvas
    bwCtx.putImageData(imageData, 0, 0);
};
const calculateRecommendedDotCount = () => {
    if (!img)
        return 0;
    const imageData = bwCtx.getImageData(0, 0, bwCanvas.width, bwCanvas.height);
    const data = imageData.data;
    let blackPixels = 0;
    for (let i = 0; i < data.length; i += 4) {
        if (data[i] === 0) {
            blackPixels++;
        }
    }
    const totalPixels = img.width * img.height;
    const blackPercentage = blackPixels / totalPixels;
    recommendedDotCount = Math.ceil(blackPercentage * 200000);
    if (dotCountElement) {
        dotCountElement.textContent = `Recommended dot count: ${recommendedDotCount}`;
        dotCountElement.style.display = "block";
    }
    return recommendedDotCount;
};
