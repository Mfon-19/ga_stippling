"use strict";
// Each individual has dot count of 5000
// Each dot has an x, y, and radius value
class Individual {
    constructor(width, height) {
        this.fitness = 0;
        const dotCount = 50000;
        this.width = width;
        this.height = height;
        this.dots = new Array(dotCount);
        for (let i = 0; i < dotCount; i++) {
            this.dots[i] = new Dot(this.randX(), this.randY(), this.randRadius());
        }
    }
    mutate(mutationRate) {
        // Generate new random values for some the x, y, and radius values
        for (let i = 0; i < this.dots.length; i++) {
            this.dots[i].x =
                Math.random() < mutationRate ? this.randX() : this.dots[i].x;
            this.dots[i].y =
                Math.random() < mutationRate ? this.randY() : this.dots[i].y;
            // Add radius mutation
            this.dots[i].radius =
                Math.random() < mutationRate
                    ? this.randRadius() // Random radius between 1-5
                    : this.dots[i].radius;
        }
    }
    randX() {
        return Math.floor(Math.random() * this.width);
    }
    randY() {
        return Math.floor(Math.random() * this.height);
    }
    randRadius() {
        return 1 + Math.random() * 2;
    }
}
