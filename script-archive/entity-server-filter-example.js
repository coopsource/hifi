function filter(p) {
    /* block comments are ok, but not double-slash end-of-line-comments */

    /* Simple example: if someone specifies name, add an 'x' to it. Note that print is ok to use. */
    if (p.name) {p.name += 'x'; print('fixme name', p. name);}

    /* This example clamps y. A better filter would probably zero y component of velocity and acceleration. */
    if (p.position) {p.position.y = Math.min(1, p.position.y); print('fixme p.y', p.position.y);}

    /* Can also reject altogether */
    if (p.userData) { return false; }
	
    /* Reject if modifications made to Model properties */
    if (p.modelURL || p.compoundShapeURL || p.shape || p.shapeType || p.url || p.fps || p.currentFrame || p.running || p.loop || p.firstFrame || p.lastFrame || p.hold || p.textures || p.xTextureURL || p.yTextureURL || p.zTextureURL) { return false; }
	
	/* Clamp velocity to maxVelocity units/second. Zeroing each component of acceleration keeps us from slamming.*/
	var maxVelocity = 5;
    if (p.velocity) {
		if (Math.abs(p.velocity.x) > maxVelocity) {
			p.velocity.x = Math.sign(p.velocity.x) * maxVelocity;
			p.acceleration.x = 0;
		}
		if (Math.abs(p.velocity.y) > maxVelocity) {
			p.velocity.y = Math.sign(p.velocity.y) * maxVelocity;
			p.acceleration.y = 0;
		}
		if (Math.abs(p.velocity.z) > maxVelocity) {
			p.velocity.z = Math.sign(p.velocity.z) * maxVelocity;
			p.acceleration.z = 0;
		}
    }
    
    /* Define an axis-aligned zone in which entities are not allowed to enter. */
    /* This example zone corresponds to an area to the right of the spawnpoint
    in your Sandbox. It's an area near the big rock to the right. If an entity
    enters the zone, it'll move behind the rock.*/
    var boxMin = {x: 25.5, y: -0.48, z: -9.9};
    var boxMax = {x: 31.1, y: 4, z: -3.79};
    var zero = {x: 0.0, y: 0.0, z: 0.0};
    
    if (p.position) {
        var x = p.position.x;
        var y = p.position.y;
        var z = p.position.z;
        if ((x > boxMin.x && x < boxMax.x) &&
            (y > boxMin.y && y < boxMax.y) &&
            (z > boxMin.z && z < boxMax.z)) {
            /* Move it to the origin of the zone */
            p.position = boxMin;
            p.velocity = zero;
            p.acceleration = zero;
        }
    }

    /* If we make it this far, return the (possibly modified) properties. */
    return p;
}
