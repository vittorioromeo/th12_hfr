/* Pure geometry regression coverage shared by the native-60 adapters. */
static void test_fixed_quad(void) {
    struct FixedQuadHistory h = {0};
    float p[4][3]={{-2,-2,0},{2,-2,0},{-2,2,0},{2,2,0}}, out[4][3];
    assert(!fixed_quad_pose(&h,1,2,1,1,p,0.5,0,out));
    for (int i=0;i<4;++i) p[i][0]+=8;
    assert(fixed_quad_pose(&h,1,2,2,2,p,0.5,0,out));
    assert(fabsf(out[0][0]-2)<1e-5f);
    assert(fixed_quad_pose(&h,1,2,2,2,p,0.5,1,out));
    assert(fabsf(out[0][0]-10)<1e-5f);
    /* One VM reused for two glyphs must stay untracked across subsequent ticks. */
    p[0][0]+=1;
    assert(!fixed_quad_pose(&h,1,2,2,2,p,0.5,0,out));
    p[0][0]+=1;
    assert(!fixed_quad_pose(&h,1,2,3,3,p,0.5,0,out));
    assert(!fixed_quad_pose(&h,1,3,4,0,p,0.5,0,out)); /* new script */
    assert(!fixed_quad_pose(&h,3,3,6,1,p,0.5,0,out)); /* key/gap */
    for(int i=0;i<4;++i) p[i][0]+=100;
    assert(!fixed_quad_pose(&h,3,3,7,2,p,0.5,0,out)); /* teleport */
    p[0][0]=NAN;
    assert(!fixed_quad_pose(&h,3,3,8,3,p,0.5,0,out));
    /* A rigid rotation keeps its radius rather than shrinking along a chord. */
    memset(&h,0,sizeof h);
    for(int i=0;i<4;++i) {h.previous[i][0]=(i&1)?2:-2;h.previous[i][1]=(i&2)?2:-2;
        h.current[i][0]=h.previous[i][0]*cosf(0.4f)-h.previous[i][1]*sinf(0.4f);
        h.current[i][1]=h.previous[i][0]*sinf(0.4f)+h.previous[i][1]*cosf(0.4f);}
    for(int predict=0;predict<2;++predict) {
        fixed_quad_shape(&h,0.5,predict,out);
        for(int i=0;i<4;++i) assert(fabsf(out[i][0]*out[i][0]+out[i][1]*out[i][1]-8)<1e-4f);
    }
    puts("PASS: shared quad interpolation/prediction, rotation, VM reuse, script reset, gaps and teleports");
}
