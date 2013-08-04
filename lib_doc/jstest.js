/**
 * Created with JetBrains WebStorm.
 * User: jie
 * Date: 13-8-4
 * Time: 下午1:52
 * To change this template use File | Settings | File Templates.
 */
var c = {name: 'a', wrap: function () {
}};
function fn(ex) {
    console.log(this);
    var global = this;

    global.__defineGetter__('def1', function() {
        return this.phone;
    });
    ex.__defineGetter__('def2', function() {
        return this.phone;
    });

    var add = 'hz';
    ex.test = function () {
        console.log('test', this.name);
    }
    function test2() {
    }

    console.log('binding 01', this.name);
    console.log('binding 02', this.phone);
    console.log('binding 03 def2:', def2);
    console.log('binding 03 def1:', def1);
    console.log('binding 04', phone);
}
var obj1 = {phone: '138'};
fn.call(obj1, c);

console.log(c.test());
console.log(obj1.test2());
console.log(c.test2());