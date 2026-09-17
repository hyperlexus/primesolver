A=[str(n)for n in range(1000,10000)if all(n%i for i in range(2,n))]
P=A*1
def F(g,t):
    r,t=list('nnnn'),list(t)
    for i in 0,1,2,3:
        if g[i]==t[i]:r[i]='g';t[i]=0
    for i in 0,1,2,3:
        if r[i]=='n'and g[i]in t:r[i]='y';t[t.index(g[i])]=0
    return"".join(r)
while P[1:]:
    P.sort(key=lambda x:len(set(x)))
    g=(len(P)<9 and next((x for x in A if len({F(x,p)for p in P})==len(P)),0))or P[-1]
    b=input(g)
    P=[p for p in P if F(g,p)==b]
print(P[0])