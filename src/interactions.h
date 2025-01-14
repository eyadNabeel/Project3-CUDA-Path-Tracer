#pragma once

#include "intersections.h"

#define DIRECTLIGHT 1

// CHECKITOUT
/**
 * Computes a cosine-weighted random direction in a hemisphere.
 * Used for diffuse lighting.
 */
__host__ __device__
glm::vec3 calculateRandomDirectionInHemisphere(
        glm::vec3 normal, thrust::default_random_engine &rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;

    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(1, 0, 0);
    } else if (abs(normal.y) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(0, 1, 0);
    } else {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

__host__ __device__
glm::vec3 sampleSphereLight(
    glm::vec3 pos, glm::vec3 scale, thrust::default_random_engine& rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

    float phi = u01(rng) * 2.f * glm::pi<float>();
    float theta = u01(rng) * 2.f * glm::pi<float>();
    glm::vec3 unit_cartesian = glm::vec3(glm::cos(theta) * glm::sin(phi),
        glm::sin(theta) * glm::sin(phi),
        glm::cos(phi));
    return unit_cartesian * scale + pos;
}

__host__ __device__
glm::vec3 sampleCubeLight(
    glm::vec3 pos, glm::vec3 scale, thrust::default_random_engine& rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

    glm::vec3 sample(u01(rng), u01(rng), u01(rng));
    return sample * scale + pos;
}


/**
 * Scatter a ray with some probabilities according to the material properties.
 * For example, a diffuse surface scatters in a cosine-weighted hemisphere.
 * A perfect specular surface scatters in the reflected ray direction.
 * In order to apply multiple effects to one surface, probabilistically choose
 * between them.
 *
 * The visual effect you want is to straight-up add the diffuse and specular
 * components. You can do this in a few ways. This logic also applies to
 * combining other types of materias (such as refractive).
 *
 * - Always take an even (50/50) split between a each effect (a diffuse bounce
 *   and a specular bounce), but divide the resulting color of either branch
 *   by its probability (0.5), to counteract the chance (0.5) of the branch
 *   being taken.
 *   - This way is inefficient, but serves as a good starting point - it
 *     converges slowly, especially for pure-diffuse or pure-specular.
 * - Pick the split based on the intensity of each material color, and divide
 *   branch result by that branch's probability (whatever probability you use).
 *
 * This method applies its changes to the Ray parameter `ray` in place.
 * It also modifies the color `color` of the ray in place.
 *
 * You may need to change the parameter list for your purposes!
 */

__host__ __device__ glm::vec3 jitterRay(glm::vec3 direction, thrust::default_random_engine& rng, float radius) {
    thrust::uniform_real_distribution<float> u01(0, 1);
    return glm::normalize(direction + glm::normalize(glm::vec3(u01(rng), u01(rng), u01(rng))) * radius);
}

__host__ __device__
void scatterRay(
        PathSegment & pathSegment,
        glm::vec3 intersect,
        glm::vec3 normal,
        const Material &m,
        Geom *lights,
        thrust::default_random_engine &rng,
        int num_lights,
        int idx) {

    pathSegment.color *= m.color;
    thrust::uniform_real_distribution<float> u01(0, 1);
    
    if (m.hasReflective > 0.f) {
        pathSegment.ray.direction = (u01(rng) < m.hasReflective) ? jitterRay(glm::reflect(pathSegment.ray.direction, normal), rng, 0.f) : calculateRandomDirectionInHemisphere(normal, rng);
    }
    else if (m.hasRefractive > 0.f) {
        float costheta = glm::dot(normal, -pathSegment.ray.direction);
        bool entering = costheta > 0;
        float r_not = glm::pow((1.5 - 1) / (1.5 + 1), 2);
        float r_theta = r_not + (1 - r_not) * glm::pow((1 - costheta), 5);
        float eta = (entering) ? 1 / 1.5 : 1.5 / 1;

        glm::vec3 refracted = glm::refract(pathSegment.ray.direction, normal, eta);
        glm::vec3 reflected = jitterRay(glm::reflect(pathSegment.ray.direction, normal), rng, 0.f);

        if (u01(rng) < r_theta || glm::abs(glm::dot(normal, refracted)) < 0.001) {
            pathSegment.ray.direction = jitterRay(glm::reflect(pathSegment.ray.direction, normal), rng, 0.f);
        }
        else {
            pathSegment.ray.direction = glm::refract(pathSegment.ray.direction, normal, eta);
        }
    }
#if DIRECTLIGHT == 1
    else if (pathSegment.remainingBounces == 1) {
        int light_idx = glm::floor(u01(rng) * num_lights);
        Geom light = lights[light_idx];
        glm::vec3 sample;
        switch (light.type) {
        case SPHERE:
            sample = sampleSphereLight(light.translation, light.scale, rng);
            break;
        case CUBE:
            sample = sampleCubeLight(light.translation, light.scale, rng);
            break;
        }
        pathSegment.ray.direction = glm::normalize(sample - pathSegment.ray.origin);
    }
#endif
    else {
        pathSegment.ray.direction = calculateRandomDirectionInHemisphere(normal, rng);
    }

    pathSegment.ray.origin = intersect + pathSegment.ray.direction * 0.001f;
 }

