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
if (!imgCtx || !bwCtx || !ctx || !evolCtx)
    throw new Error("Failed to get canvas contexts");
ctx.fillStyle = "black";
const endAngle = 2 * Math.PI;
let generations = 0;
const startEvolution = () => {
    if (!img)
        return;
    const algo = new GeneticAlgorithm(100, 0.01, bwCtx.getImageData(0, 0, img === null || img === void 0 ? void 0 : img.width, img === null || img === void 0 ? void 0 : img.height), ctx);
    const intervalId = setInterval(() => {
        algo.evolve();
        generations++;
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
            // Automatically convert to B&W when the image loads
            convertToBlackAndWhite();
            // Don't automatically start evolution - wait for button click
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
    // Draw the black and white image on the second canvas
    bwCtx.putImageData(imageData, 0, 0);
};
