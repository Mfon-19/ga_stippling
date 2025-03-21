"use strict";
// A population of Individuals. Population size is provided in the constructor
class Population {
    constructor(size, target, ctx) {
        this.size = size;
        this.target = target;
        this.population = Array(size);
        this.targetData = Array.from(target.data);
        this.ctx = ctx;
        // Create offscreen canvas with same dimensions as target
        this.offscreenCanvas = new OffscreenCanvas(target.width, target.height);
        this.offscreenCtx = this.offscreenCanvas.getContext("2d", {
            willReadFrequently: true,
        });
        // Initialize population
        for (let i = 0; i < size; i++) {
            this.population[i] = new Individual(target.width, target.height);
        }
    }
    calculateFitness() {
        for (let individual of this.population) {
            // Draw to offscreen canvas instead of main canvas
            this.drawIndividualOffscreen(individual.dots);
            // Get image data from offscreen canvas
            const imageData = this.offscreenCtx.getImageData(0, 0, this.target.width, this.target.height).data;
            let diff = 0;
            const totalPixels = this.target.width * this.target.height;
            for (let i = 0; i < totalPixels; i++) {
                const p = i * 4;
                // Compare only the first channel for B&W images
                const pixelDiff = imageData[p] - this.targetData[p];
                diff += pixelDiff * pixelDiff;
            }
            const maxPossibleDiff = totalPixels * 255 * 255;
            individual.fitness = 1 - diff / maxPossibleDiff;
            // Ensure fitness is never exactly zero
            if (individual.fitness < 0.001) {
                individual.fitness = 0.001;
            }
        }
    }
    drawIndividualOffscreen(dots) {
        if (!this.offscreenCtx)
            return;
        // Clear the offscreen canvas
        this.offscreenCtx.clearRect(0, 0, this.offscreenCanvas.width, this.offscreenCanvas.height);
        // White background
        this.offscreenCtx.fillStyle = "white";
        this.offscreenCtx.fillRect(0, 0, this.offscreenCanvas.width, this.offscreenCanvas.height);
        // Draw black dots
        this.offscreenCtx.fillStyle = "black";
        // Draw each dot in the Individual representation
        for (let i = 0; i < dots.length; i++) {
            this.offscreenCtx.beginPath();
            this.offscreenCtx.arc(dots[i].x, dots[i].y, dots[i].radius, 0, 2 * Math.PI);
            this.offscreenCtx.fill();
        }
    }
    // Keep existing drawIndividual method for displaying the fittest individual
    drawIndividual(dots, ctx) {
        if (!ctx)
            return null;
        ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        ctx.fillStyle = "white";
        ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        ctx.fillStyle = "black";
        for (let i = 0; i < dots.length; i++) {
            ctx.beginPath();
            ctx.arc(dots[i].x, dots[i].y, dots[i].radius, 0, 2 * Math.PI);
            ctx.fill();
        }
        return ctx;
    }
    fittestIndividual() {
        let fittest = 0;
        this.population.forEach((individual, index) => (fittest =
            this.population[fittest].fitness > individual.fitness
                ? fittest
                : index));
        return fittest;
    }
}
